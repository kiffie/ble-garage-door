/*
 * BLE garage door opener remote control
 *
 * Copyright (C) 2020, Stephan <kiffie@mailbox.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *
 * Storage functions
 */

#ifndef __STORAGE_H__
#define __STORAGE_H__

#include <fds.h>
#include <ble.h>

ret_code_t gds_init(void);

/* create a new TX record if it does not exist.
 * returns true on success (i.e. record exists or was successfully created)
 */
bool gds_create_tx_record(const ble_uuid128_t *uuid);

/** Get stored sequence number of a specific transmitter
 * Sets *seq_no to zero if no sequence number record exists.
 * Returns false if transmitter is unknown
 */
bool gds_get_seq_no(const ble_uuid128_t *uuid, uint32_t *seq_no);

/** Set sequence number of specific transmitter.
 * returns false if transmitter is unknown
 */
bool gds_set_seq_no(const ble_uuid128_t *uuid, uint32_t seq_no);

/** Run tasks (currently used for garbage collection)
 */
void gds_tasks();

/** Clear all transmitter related information
 */
void gds_clear(void);

/** Dump the storage content to the debug log
 */
void gds_dump_to_log(void);

#endif
