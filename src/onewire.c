/* onewire.c
   Copyright (C) 2024 Timo Kokkonen <tjko@iki.fi>

   SPDX-License-Identifier: GPL-3.0-or-later

   This file is part of FanPico.

   FanPico is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   FanPico is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with FanPico. If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "pico/stdlib.h"
#include "pico_1wire.h"

#include "fanpico.h"

#define MAX_1WIRE_DEVICES   32


typedef struct onewire_state_t {
	pico_1wire_t *ctx;
	uint devices;
	uint64_t addr[MAX_1WIRE_DEVICES];
} onewire_state_t;

onewire_state_t onewire_state_struct;
onewire_state_t *onewire_bus = &onewire_state_struct;

void onewire_scan_bus()
{
	int res;
	bool psu;
	uint count;

	if (!onewire_bus->ctx)
		return;

	log_msg(LOG_INFO, "Scanning 1-Wire bus...");

	/* Check for any devices on the bus... */
	res = pico_1wire_reset_bus(onewire_bus->ctx);
	if (!res) {
		log_msg(LOG_INFO, "No devices found in 1-Wire bus.");
		return;
	}

	/* Check if any device requires "phantom" power... */
	res = pico_1wire_read_power_supply(onewire_bus->ctx, 0, &psu);
	if (psu) {
		log_msg(LOG_INFO, "All devices in 1-Wire bus have power.");
	} else {
		log_msg(LOG_INFO, "1-Wire bus has devices requiring 'phantom' power.");
	}

	/* Search for devices... */
	res = pico_1wire_search_rom(onewire_bus->ctx, onewire_bus->addr,
				MAX_1WIRE_DEVICES, &count);
	if (res) {
		log_msg(LOG_NOTICE, "1-Wire Search ROM Addresses failed: %d", res);
		return;
	}

	if (onewire_bus->devices != count) {
		log_msg(LOG_INFO, "1-Wire device count change detected: %u -> %u",
			onewire_bus->devices, count);
		for (int i = 0; i < count; i++) {
			log_msg(LOG_INFO, "1-Wire Device%d: %016llx",
				i + 1, onewire_bus->addr[i]);
		}
		onewire_bus->devices = count;
	}
}


void setup_onewire_bus()
{
	memset(onewire_bus, 0, sizeof(onewire_state_t));

#if ONEWIRE_SUPPORT
	log_msg(LOG_NOTICE, "Initializing 1-Wire Bus...");

	onewire_bus->ctx = pico_1wire_init(ONEWIRE_PIN, -1, true);
	if (!onewire_bus->ctx) {
		log_msg(LOG_ERR, "1-Wire intialization failed!");
		return;
	}

	onewire_scan_bus();
#endif
}
