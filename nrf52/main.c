/*
 * BLE garage door opener remote control
 *
 * Copyright (C) 2020, Stephan <kiffie@mailbox.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include <gd_config.h>
#include <rxm_key.h>
#include <storage.h>

#include <mbedtls/md.h>
#include <nrf_atfifo.h>

#include <nordic_common.h>
#include <nrf.h>
#include <app_error.h>
#include <ble.h>
#include <nrf_sdh.h>
#include <nrf_sdh_soc.h>
#include <nrf_sdh_ble.h>
#include <app_timer.h>
#include <fds.h>
#include <nrf_pwr_mgmt.h>
#include <nrfx_wdt.h>
#include <nrf_gpio.h>
#include <nrf_log.h>
#include <nrf_log_ctrl.h>
#include <nrf_log_default_backends.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define GD_LEARN_DURATION_MS      (10 * 1000)
#define GD_RX_DISABLE_DURATION_MS 1000

#define APP_BLE_OBSERVER_PRIO 3
#define APP_BLE_CONN_CFG_TAG  1

#define DEAD_BEEF 0xDEADBEEF /**< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */

static uint8_t scan_buffer[BLE_GAP_SCAN_BUFFER_MAX];

APP_TIMER_DEF(timer_periodic);
static uint64_t timer_ticks = 0;
static unsigned gd_relay_timer;
static unsigned gd_button_presssed_ctr = 0;
static unsigned gd_learn_ctr = 0;
static unsigned gd_rx_disable_ctr = 0;

typedef enum {
    GD_BUTCMD_NONE,
    GD_BUTCMD_LEARN,
    GD_BUTCMD_CLEAR,
    GD_BUTCMD_CONSUMED, /* to indicate that command value was read */
} gd_button_cmd_t;

static gd_button_cmd_t gd_button_cmd = GD_BUTCMD_NONE;

typedef struct {
    uint8_t cmd;       /* command byte */
    uint8_t seq_no[3]; /* sequence number (big endian) */
    uint8_t digest[4]; /* first four octets of HMAC-SHA256 */
} gd_message_t;

typedef struct {
    ble_uuid128_t uuid;
    gd_message_t msg;
    int8_t rssi;
} gd_adv_data_t;

NRF_ATFIFO_DEF(gd_adv_fifo, gd_adv_data_t, 2);

/* calculate transmitter key from transmitter UUID */
static void gd_calculate_tx_key(const ble_uuid128_t *tx_uuid, uint8_t key[32]) {
    /* convert UUID into big endian representation */
    uint8_t uuid_be[16];
    for (int i = 0; i < 16; i++) {
        uuid_be[15 - i] = tx_uuid->uuid128[i];
    }
    APP_ERROR_CHECK_BOOL(0 == mbedtls_md_hmac(
                                  mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                                  gd_rxm_key, GD_RXM_KEY_SIZE,
                                  uuid_be, 16,
                                  key));
}

static bool gd_msg_check_digest(const ble_uuid128_t *uuid, const gd_message_t *msg) {
    uint8_t key[32];
    gd_calculate_tx_key(uuid, key);
    uint8_t digest[32];

    APP_ERROR_CHECK_BOOL(0 == mbedtls_md_hmac(
                                  mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                                  key, sizeof(key),
                                  (uint8_t *)msg, 4,
                                  digest));
    return memcmp(digest, msg->digest, 4) == 0;
}

static uint32_t gd_msg_get_seqno(const gd_message_t *msg) {
    return (msg->seq_no[0] << 16) | (msg->seq_no[1] << 8) | msg->seq_no[2];
}

/**@brief Callback function for asserts in the SoftDevice.
 *
 * @details This function will be called in case of an assert in the SoftDevice.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyze
 *          how your product is supposed to react in case of Assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in] line_num   Line number of the failing ASSERT call.
 * @param[in] file_name  File name of the failing ASSERT call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t *p_file_name) {
    app_error_handler(DEAD_BEEF, line_num, p_file_name);
}

static inline unsigned timer_ticks_from_ms(unsigned ms) {
    return ms / 100;
}

static uint64_t timer_now(void) {
    uint64_t now;
    CRITICAL_REGION_ENTER();
    now = timer_ticks;
    CRITICAL_REGION_EXIT();
    return now;
}

static void timer_delay_ms(unsigned ms) {
    uint64_t when = timer_now() + timer_ticks_from_ms(ms);
    while (when > timer_now()) {}
}

static void timer_tick_handler(void *dummy) {
    timer_ticks++;
    if (gd_relay_timer > 0) {
        gd_relay_timer--;
        if (gd_relay_timer == 0) {
            nrf_gpio_pin_clear(GD_PINNO_RELAY);
        }
    }
    if (gd_learn_ctr > 0) {
        gd_learn_ctr--;
    }
    if (gd_rx_disable_ctr > 0) {
        gd_rx_disable_ctr--;
    }
    switch (gd_button_cmd) {
        case GD_BUTCMD_NONE:
            if (nrf_gpio_pin_read(GD_PINNO_BUTTON)) { /* button pressed */
                if (++gd_button_presssed_ctr >= timer_ticks_from_ms(5000)) {
                    gd_button_cmd = GD_BUTCMD_CLEAR;
                }
            } else {
                if (gd_button_presssed_ctr >= timer_ticks_from_ms(100)) {
                    gd_button_cmd = GD_BUTCMD_LEARN;
                }
                gd_button_presssed_ctr = 0;
            }
            break;
        case GD_BUTCMD_CONSUMED:
            if (!nrf_gpio_pin_read(GD_PINNO_BUTTON)) { /* button released */
                gd_button_presssed_ctr = 0;
                gd_button_cmd = GD_BUTCMD_NONE;
            }
            break;
        default:
            break;
    }
}

static gd_button_cmd_t gd_get_button(void) {
    gd_button_cmd_t r;
    CRITICAL_REGION_ENTER();
    r = gd_button_cmd;
    switch (r) {
        case GD_BUTCMD_NONE:
            break;
        case GD_BUTCMD_CONSUMED:
            r = GD_BUTCMD_NONE;
            break;
        default:
            gd_button_cmd = GD_BUTCMD_CONSUMED;
            break;
    }
    CRITICAL_REGION_EXIT();
    return r;
}

static void timer_init(void) {

    APP_ERROR_CHECK(app_timer_init());
    APP_ERROR_CHECK(app_timer_create(&timer_periodic,
                                     APP_TIMER_MODE_REPEATED,
                                     timer_tick_handler));
    APP_ERROR_CHECK(app_timer_start(timer_periodic,
                                    APP_TIMER_TICKS(100),
                                    NULL));
}

static void gd_activate_relay(void) {
    CRITICAL_REGION_ENTER();
    gd_relay_timer = timer_ticks_from_ms(1000);
    CRITICAL_REGION_EXIT();
    nrf_gpio_pin_set(GD_PINNO_RELAY);
}

static bool gd_is_relay_active(void) {
    bool r;
    CRITICAL_REGION_ENTER();
    r = gd_relay_timer > 0;
    CRITICAL_REGION_EXIT();
    return r;
}

static bool gd_is_learning(void) {
    bool r;
    CRITICAL_REGION_ENTER();
    r = gd_learn_ctr > 0;
    CRITICAL_REGION_EXIT();
    return r;
}

static bool gd_is_rx_disabled(void) {
    bool r;
    CRITICAL_REGION_ENTER();
    r = gd_rx_disable_ctr > 0;
    CRITICAL_REGION_EXIT();
    return r;
}

static void gd_gpio_init(void) {
    /* LED */
    nrf_gpio_cfg(GD_PINNO_LED,
                 NRF_GPIO_PIN_DIR_OUTPUT,
                 NRF_GPIO_PIN_INPUT_DISCONNECT,
                 NRF_GPIO_PIN_NOPULL,
                 NRF_GPIO_PIN_S0H1,
                 NRF_GPIO_PIN_NOSENSE);
    /* Relay*/
    nrf_gpio_cfg_output(GD_PINNO_RELAY);

    /* Button */
    nrf_gpio_cfg_sense_input(GD_PINNO_BUTTON,
                             NRF_GPIO_PIN_PULLDOWN,
                             NRF_GPIO_PIN_NOSENSE);
}

static void gd_set_led(bool on) {
    if (on) {
        nrf_gpio_pin_set(GD_PINNO_LED);
    } else {
        nrf_gpio_pin_clear(GD_PINNO_LED);
    }
}

/*
 * process Advertising Data (AD)
 * see Supplement to Core Spec. (CSS Version 7)
 * see Assigned Numbers for GAP
 * (https://www.bluetooth.com/specifications/assigned-numbers/generic-access-profile/)
 */

#define AD_TYPE_SERVICE_DATA128 0x21

static void handle_adv_data(const gd_adv_data_t *ad) {
    if (gd_is_rx_disabled()) {
        NRF_LOG_DEBUG("dropping data");
        return;
    }
    uint32_t seq_no = gd_msg_get_seqno(&ad->msg);
    bool digest_ok = gd_msg_check_digest(&ad->uuid, &ad->msg);
    NRF_LOG_DEBUG("UUID"); NRF_LOG_HEXDUMP_DEBUG(ad->uuid.uuid128, 16);
    NRF_LOG_DEBUG("Message"); NRF_LOG_HEXDUMP_DEBUG(&ad->msg, 8);
    NRF_LOG_DEBUG("Sequence number: %u", seq_no);
    NRF_LOG_DEBUG("Digest check: %d", digest_ok);
    if (!digest_ok) {
        /* disable receiver for a while for security reasons to prevent brute
         * force attacks (most probably not needed due to the low throughput of
         * the BLE advertising procedures) */
        CRITICAL_REGION_ENTER();
        gd_rx_disable_ctr = timer_ticks_from_ms(GD_RX_DISABLE_DURATION_MS);
        CRITICAL_REGION_EXIT();
        return;
    }
    uint32_t stored_seq_no;
    if (gds_get_seq_no(&ad->uuid, &stored_seq_no)) {
        NRF_LOG_DEBUG("stored_seq_no = %u", stored_seq_no);
        if (seq_no > stored_seq_no) {
            NRF_LOG_DEBUG("sequence number is valid");
            gds_set_seq_no(&ad->uuid, seq_no);
            gd_activate_relay();
        } else {
            NRF_LOG_INFO("invalid sequence number %u <= %d for UUID:",
                         seq_no, stored_seq_no);
            NRF_LOG_HEXDUMP_INFO(ad->uuid.uuid128, 16);
        }
    } else if (gd_is_learning()) {
        NRF_LOG_INFO("creating new transmitter record");
        gds_create_tx_record(&ad->uuid);
        gds_set_seq_no(&ad->uuid, seq_no);
    } else {
        NRF_LOG_INFO("unknown transmitter");
    }
}

static void handle_adv_report(uint8_t *data, size_t len, int8_t rssi) {
    //NRF_LOG_DEBUG("GAP Advertising report, len=%u, RSSI=%d.", len, rssi);
    //NRF_LOG_HEXDUMP_DEBUG(data, len);

    size_t ndx = 0;
    while (ndx < len) {
        size_t l = data[ndx++];
        if (ndx + l > len) { // length l to large
            NRF_LOG_INFO("invalid length field in Advertising Data");
            break;
        }
        uint8_t t = data[ndx];
        switch (t) {
            case AD_TYPE_SERVICE_DATA128:
                if (l != 25) { /* 1 type octet, 16 octets UUID, 8 octets message */
                    break;
                }
                const ble_uuid128_t *uuid = (ble_uuid128_t *)&data[ndx + 1];
                const gd_message_t *msg = (gd_message_t *)&data[ndx + 17];
                nrf_atfifo_item_put_t fifo_context;
                gd_adv_data_t *ad = nrf_atfifo_item_alloc(gd_adv_fifo, &fifo_context);
                if (ad != NULL) {
                    memcpy(&ad->uuid, uuid, sizeof(ad->uuid));
                    memcpy(&ad->msg, msg, sizeof(ad->msg));
                    ad->rssi = rssi;
                    nrf_atfifo_item_put(gd_adv_fifo, &fifo_context);
                } else {
                    NRF_LOG_INFO("ADV FIFO full");
                }
                break;
        }
        ndx += l;
    }
}

/**@brief Function for handling BLE events.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 * @param[in]   p_context   Unused.
 */
static void ble_evt_handler(ble_evt_t const *p_ble_evt, void *p_context) {
    ret_code_t err_code = NRF_SUCCESS;

    switch (p_ble_evt->header.evt_id) {

        case BLE_GAP_EVT_ADV_REPORT:;
            int8_t rssi = p_ble_evt->evt.gap_evt.params.adv_report.rssi;
            uint8_t *adv_data = p_ble_evt->evt.gap_evt.params.adv_report.data.p_data;
            size_t adv_len = p_ble_evt->evt.gap_evt.params.adv_report.data.len;
            handle_adv_report(adv_data, adv_len, rssi);
            ble_data_t scan_data = {
                .p_data = scan_buffer,
                .len = sizeof(scan_buffer)};
            err_code = sd_ble_gap_scan_start(NULL, &scan_data);
            APP_ERROR_CHECK(err_code);
            break;

        default:
            NRF_LOG_DEBUG("ble_evt_handler: evt_id = %02x", p_ble_evt->header.evt_id);
            // No implementation needed.
            break;
    }
}

/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void) {
    ret_code_t err_code;

    err_code = nrf_sdh_enable_request();
    APP_ERROR_CHECK(err_code);

    // Configure the BLE stack using the default settings.
    // Fetch the start address of the application RAM.
    uint32_t ram_start = 0;
    err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
    APP_ERROR_CHECK(err_code);

    // Enable BLE stack.
    err_code = nrf_sdh_ble_enable(&ram_start);
    APP_ERROR_CHECK(err_code);

    // Register a handler for BLE events.
    NRF_SDH_BLE_OBSERVER(gd_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
}

static void scan_init(void) {
    APP_ERROR_CHECK(NRF_ATFIFO_INIT(gd_adv_fifo));
    ble_gap_scan_params_t params = {
        .extended = 0,
        .report_incomplete_evts = 0,
        .active = 0,
        .filter_policy = BLE_GAP_SCAN_FP_ACCEPT_ALL,
        .scan_phys = BLE_GAP_PHY_1MBPS,
        .interval = MSEC_TO_UNITS(50, UNIT_0_625_MS),
        .window = MSEC_TO_UNITS(30, UNIT_0_625_MS),
        .timeout = BLE_GAP_SCAN_TIMEOUT_UNLIMITED,
        .channel_mask = {0, 0, 0, 0, 0}};
    ble_data_t data = {
        .p_data = scan_buffer,
        .len = sizeof(scan_buffer)};
    uint32_t err_code = sd_ble_gap_scan_start(&params, &data);
    APP_ERROR_CHECK(err_code);
}

void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info) {
    NRF_LOG_ERROR("Fatal error: id = %u, pc = %08x, info = %08x", id, pc, info);
    NRF_LOG_ERROR("Waiting for WDT reset...");
    NRF_LOG_FINAL_FLUSH();
    while (true)
        ;
}

/**@brief Function for application main entry.
 */
int main(void) {
    gd_gpio_init();
    APP_ERROR_CHECK(NRF_LOG_INIT(NULL));
    NRF_LOG_DEFAULT_BACKENDS_INIT();

    nrfx_wdt_config_t wdt_config = NRFX_WDT_DEAFULT_CONFIG;
    APP_ERROR_CHECK(nrfx_wdt_init(&wdt_config, NULL)); /* no IRQs used/enabled */
    nrfx_wdt_channel_id wdt_channel;
    APP_ERROR_CHECK(nrfx_wdt_channel_alloc(&wdt_channel));
    nrfx_wdt_enable();

    timer_init();
    APP_ERROR_CHECK(nrf_pwr_mgmt_init());
    APP_ERROR_CHECK(gds_init());
    gds_dump_to_log();
    ble_stack_init();
    scan_init();

    NRF_LOG_DEBUG("Initialized.");

    // Enter main loop.
    for (;;) {
        nrf_atfifo_item_get_t fifo_context;
        gd_adv_data_t *ad = nrf_atfifo_item_get(gd_adv_fifo, &fifo_context);
        if (ad != NULL) {
            handle_adv_data(ad);
            nrf_atfifo_item_free(gd_adv_fifo, &fifo_context);
        }

        /* LED control */
        if (gd_is_learning()) {
            gd_set_led((gd_learn_ctr / timer_ticks_from_ms(500)) & 0x01);
        } else {
            gd_set_led(gd_is_relay_active());
        }
        switch (gd_get_button()) {
            case GD_BUTCMD_LEARN:
                NRF_LOG_DEBUG("button command GD_BUTCMD_LEARN");
                CRITICAL_REGION_ENTER();
                gd_learn_ctr = timer_ticks_from_ms(GD_LEARN_DURATION_MS);
                CRITICAL_REGION_EXIT();
                break;
            case GD_BUTCMD_CLEAR:
                NRF_LOG_DEBUG("button command GD_BUTCMD_CLEAR");
                /* this will block the main loop for a while */
                for (int i = 0; i < 30; i++) {
                    gd_set_led(true);
                    timer_delay_ms(100);
                    gd_set_led(false);
                    timer_delay_ms(100);
                }
                gds_clear();
                break;
            default:
                break;
        }

        gds_tasks();

        /* log or sleep */
        if (NRF_LOG_PROCESS() == false) {
            nrf_pwr_mgmt_run();
        }
        nrfx_wdt_channel_feed(wdt_channel);
    }
}
