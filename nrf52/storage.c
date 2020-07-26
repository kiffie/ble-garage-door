/*
 * BLE garage door opener remote control
 *
 * Copyright (C) 2020, Stephan <kiffie@mailbox.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *
 * Storage functions
 */

#include <storage.h>

#include <nrf_log.h>
#include "nrf_log_ctrl.h"
#include <string.h>

#define GDS_TXINFO_FILE_ID 0x1000
#define GDS_TXREC_KEY      0x0001
#define GDS_SEQNOREC_KEY   0x0002

static volatile bool gds_init_done;
static volatile bool gds_flash_access_done;

typedef struct {
    ble_uuid128_t uuid; /* Transmitter UUID (Little Endian) */
} gds_transmitter_record_t;

typedef struct {
    uint32_t txrecid; /* record ID of transmitter record */
    uint32_t seq_no;
} gds_seq_no_record_t;

/* Get record_desc of a transmitter record specified by an UUID
 * Returns true if transmitter exists and record_desc is set accordingly
 */
static bool gds_get_tx_rec(const ble_uuid128_t *uuid, fds_record_desc_t *record_desc) {

    fds_flash_record_t record;
    fds_find_token_t ftok;

    memset(&ftok, 0x00, sizeof(fds_find_token_t));
    while (fds_record_find(GDS_TXINFO_FILE_ID, GDS_TXREC_KEY,
                           record_desc, &ftok) == NRF_SUCCESS) {
        if (fds_record_open(record_desc, &record) != NRF_SUCCESS) {
            NRF_LOG_ERROR("could not open FDS record");
            continue;
        }
        gds_transmitter_record_t *tx = (gds_transmitter_record_t *)record.p_data;
        if (memcmp(uuid, &tx->uuid, sizeof(ble_uuid128_t)) == 0) {
            APP_ERROR_CHECK(fds_record_close(record_desc));
            return true;
        }
        APP_ERROR_CHECK(fds_record_close(record_desc));
    }
    return false;
}

/* Get record ID of a transmitter record specified by an UUID
 * Returns true if transmitter exists and *id is set accordingly
 */
static bool gds_get_tx_recid(const ble_uuid128_t *uuid, uint32_t *id) {
    fds_record_desc_t record_desc;
    if (gds_get_tx_rec(uuid, &record_desc)) {
        APP_ERROR_CHECK(fds_record_id_from_desc(&record_desc, id));
        return true;
    } else {
        return false;
    }
}

/* create a new TX record if it does not exist.
 * returns true on success (i.e. record exists or was successfully created)
 */
bool gds_create_tx_record(const ble_uuid128_t *uuid) {
    fds_record_desc_t record_desc;
    if (gds_get_tx_rec(uuid, &record_desc)) {
        return true;
    } else {
        gds_transmitter_record_t recdata;
        memcpy(recdata.uuid.uuid128, uuid, sizeof(ble_uuid128_t));
        fds_record_t record = {
            .file_id = GDS_TXINFO_FILE_ID,
            .key = GDS_TXREC_KEY,
            .data = {
                .p_data = &recdata,
                .length_words = sizeof(recdata) / sizeof(uint32_t)}};
        gds_flash_access_done = false;
        ret_code_t r = fds_record_write(&record_desc, &record);
        if (r != NRF_SUCCESS) {
            NRF_LOG_ERROR("could not write TX record, result = %08x", r);
            return false;
        }
        /* wait for completion. We cannot return from the function earlier
         * because the data is stack allocated */
        NRF_LOG_DEBUG("waiting for write completion");
        while (!gds_flash_access_done) {}
        return true;
    }
}

static bool gds_find_seq_no_record(uint32_t txrecid, fds_record_desc_t *record_desc) {
    fds_flash_record_t record;
    fds_find_token_t ftok;
    memset(&ftok, 0x00, sizeof(fds_find_token_t));
    while (fds_record_find(GDS_TXINFO_FILE_ID, GDS_SEQNOREC_KEY,
                           record_desc, &ftok) == NRF_SUCCESS) {
        if (fds_record_open(record_desc, &record) != NRF_SUCCESS) {
            NRF_LOG_ERROR("could not open FDS seq_no record");
            continue;
        }
        uint32_t id = ((gds_seq_no_record_t *)record.p_data)->txrecid;
        APP_ERROR_CHECK(fds_record_close(record_desc));
        if (id == txrecid) {
            return true;
        }
    }
    return false;
}

/** Get stored sequence number of a specific transmitter
 * Sets *seq_no to zero if no sequence number record exists.
 * Returns false if transmitter is unknown
 */
bool gds_get_seq_no(const ble_uuid128_t *uuid, uint32_t *seq_no) {
    *seq_no = 0; /* default seq_no */
    uint32_t txrecid;
    if (!gds_get_tx_recid(uuid, &txrecid)) {
        return false;
    }
    fds_flash_record_t record;
    fds_record_desc_t record_desc;
    fds_find_token_t ftok;
    memset(&ftok, 0x00, sizeof(fds_find_token_t));
    while (fds_record_find(GDS_TXINFO_FILE_ID, GDS_SEQNOREC_KEY,
                           &record_desc, &ftok) == NRF_SUCCESS) {
        if (fds_record_open(&record_desc, &record) != NRF_SUCCESS) {
            NRF_LOG_ERROR("could not open FDS seq_no record");
            continue;
        }
        gds_seq_no_record_t *sn = (gds_seq_no_record_t *)record.p_data;
        if (sn->txrecid == txrecid) {
            *seq_no = sn->seq_no;
            APP_ERROR_CHECK(fds_record_close(&record_desc));
            break;
        } else {
            APP_ERROR_CHECK(fds_record_close(&record_desc));
        }
    }
    return true;
}

/** Set sequence number of specific transmitter.
 * returns false if transmitter is unknown
 */
bool gds_set_seq_no(const ble_uuid128_t *uuid, uint32_t seq_no) {
    uint32_t txrecid;
    if (!gds_get_tx_recid(uuid, &txrecid)) {
        return false;
    }
    gds_seq_no_record_t recdata = {.txrecid = txrecid, .seq_no = seq_no};
    fds_record_t record = {
        .file_id = GDS_TXINFO_FILE_ID,
        .key = GDS_SEQNOREC_KEY,
        .data = {
            .p_data = &recdata,
            .length_words = sizeof(recdata) / sizeof(uint32_t)}};
    fds_record_desc_t record_desc;
    ret_code_t r;
    gds_flash_access_done = false;
    if (gds_find_seq_no_record(txrecid, &record_desc)) {
        NRF_LOG_DEBUG("updating seq_no for record %08x to %u", txrecid, seq_no);
        r = fds_record_update(&record_desc, &record);
    } else {
        NRF_LOG_DEBUG("creating new seq_no record for %08x, seq_no = %u",
                      txrecid, seq_no);
        r = fds_record_write(NULL, &record);
    }
    if (r != NRF_SUCCESS) {
        NRF_LOG_ERROR("could not update/write seq_no record, result = %08x", r);
        gds_flash_access_done = true;
    }
    /* wait for completion. We cannot return from the function before
        * because the data is stack allocated */
    while (!gds_flash_access_done) {}
    return true;
}

static void gds_callback(fds_evt_t const *p_evt) {
    //NRF_LOG_DEBUG("GDS callback, event = %u, result = %u", p_evt->id, p_evt->result);

    switch (p_evt->id) {
        case FDS_EVT_INIT:
            NRF_LOG_DEBUG("GDS initialization complete");
            gds_init_done = true;
            break;

        case FDS_EVT_WRITE:
        case FDS_EVT_UPDATE:
            if (p_evt->write.file_id == GDS_TXINFO_FILE_ID) {
                gds_flash_access_done = true;
            }
            break;
        case FDS_EVT_DEL_RECORD:
        case FDS_EVT_DEL_FILE:
            if (p_evt->del.file_id == GDS_TXINFO_FILE_ID) {
                gds_flash_access_done = true;
            }
            break;
        case FDS_EVT_GC:
            gds_flash_access_done = true;
            break;
    }
}

void gds_clear(void) {
    NRF_LOG_INFO("Clearing all transmitter related information");
    gds_flash_access_done = false;
    if (fds_file_delete(GDS_TXINFO_FILE_ID) == NRF_SUCCESS) {
        /* wait for completion */
        while (!gds_flash_access_done) {}
    } else {
        NRF_LOG_ERROR("Could not clear transmitter related information");
    }
}

#if FDS_VIRTUAL_PAGES < 3
#error "FDS_VIRTUAL_PAGES must be at least 3"
#endif

#define GDS_GC_THRESHOLD ((FDS_VIRTUAL_PAGES - 2) * FDS_VIRTUAL_PAGE_SIZE)

void gds_tasks(void) {
    fds_stat_t stat;
    if (fds_stat(&stat) == NRF_SUCCESS) {
        if (stat.freeable_words > GDS_GC_THRESHOLD) {
            NRF_LOG_INFO("performing FDS garbage collection");
            gds_flash_access_done = false;
            if (fds_gc() != NRF_SUCCESS) {
                NRF_LOG_ERROR("Could not start garbage collection");
            } else {
                /* wait for completion */
                while (!gds_flash_access_done) {}
                NRF_LOG_INFO("garbage collection completed");
            }
        }
    }
}

ret_code_t gds_init(void) {
    gds_init_done = false;
    ret_code_t r = fds_register(gds_callback);
    if (r != NRF_SUCCESS) {
        return r;
    }
    r = fds_init();
    if (r != NRF_SUCCESS) {
        return r;
    }
    while (!gds_init_done) {}
    return NRF_SUCCESS;
}

void gds_dump_to_log(void) {
    fds_flash_record_t record;
    fds_record_desc_t record_desc;
    fds_find_token_t ftok;
    fds_stat_t stat;
    if (fds_stat(&stat) == NRF_SUCCESS) {
        NRF_LOG_DEBUG("=== GD Storage info ===");
        NRF_LOG_DEBUG("virt. page size: %u", FDS_VIRTUAL_PAGE_SIZE);
        NRF_LOG_DEBUG("GC threshold:    %u", GDS_GC_THRESHOLD);
        NRF_LOG_DEBUG("pages_available: %u", stat.pages_available);
        NRF_LOG_DEBUG("open_records:    %u", stat.open_records);
        NRF_LOG_DEBUG("valid_records:   %u", stat.valid_records);
        NRF_LOG_DEBUG("dirty_records:   %u", stat.dirty_records);
        NRF_LOG_DEBUG("words_reserved:  %u", stat.words_reserved);
        NRF_LOG_DEBUG("words_used:      %u", stat.words_used);
        NRF_LOG_DEBUG("largest_contig:  %u", stat.largest_contig);
        NRF_LOG_DEBUG("freeable_words:  %u", stat.freeable_words);
        NRF_LOG_DEBUG("corruption:      %u", stat.corruption);
    }
    NRF_LOG_DEBUG("=== GD Storage dump BEGIN ===");
    memset(&ftok, 0x00, sizeof(fds_find_token_t));
    while (fds_record_find_in_file(GDS_TXINFO_FILE_ID,
                                   &record_desc,
                                   &ftok) == NRF_SUCCESS) {
        if (fds_record_open(&record_desc, &record) != NRF_SUCCESS) {
            NRF_LOG_ERROR("could not open record for dump");
            continue;
        }
        NRF_LOG_DEBUG("key = %u, file = %u, id = %08x, len = %u",
                      record.p_header->record_key,
                      record.p_header->file_id,
                      record.p_header->record_id,
                      record.p_header->length_words * sizeof(uint32_t));
        NRF_LOG_HEXDUMP_DEBUG(record.p_data,
                              record.p_header->length_words * sizeof(uint32_t));
        APP_ERROR_CHECK(fds_record_close(&record_desc));
    }
    NRF_LOG_DEBUG("=== GD Storage dump END ===");
}
