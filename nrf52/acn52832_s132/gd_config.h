/*
 * BLE garage door opener remote control
 *
 * Copyright (C) 2020, Stephan <kiffie@mailbox.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *
 *
 * Pin assignments for a board with an ACN52832 BLE module
 *
 * P0.16    LED (pin 26)
 * P0.18    Debug TxD (pin 28)
 * P0.19    Relay (pin 29)
 * P0.20    Button (pin 30)
 *
 * P0.22    on module RGB LED red (unused)
 * P0.23    on module RGB LED blue (unused)
 * P0.24    on module RGB LED green (unused)
 *
 */

#ifndef __GD_CONFIG_H__
#define __GD_CONFIG_H__

#define GD_PINNO_LED    NRF_GPIO_PIN_MAP(0, 16)
#define GD_PINNO_RELAY  NRF_GPIO_PIN_MAP(0, 19)
#define GD_PINNO_BUTTON NRF_GPIO_PIN_MAP(0, 20)

#endif
