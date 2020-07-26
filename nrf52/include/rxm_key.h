/*
 * BLE garage door opener remote control
 *
 * Copyright (C) 2020, Stephan <kiffie@mailbox.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef __RXM_KEY__
#define __RXM_KEY__

#include <stdint.h>

/* Key used to derive individual transmitter keys. Its length can be arbitrarily
 * chosen. Here we use 20 byte (160 bits) because it can be nicely base32
 * encoded
 */

#define GD_RXM_KEY_SIZE 20

const uint8_t gd_rxm_key[GD_RXM_KEY_SIZE];

#endif
