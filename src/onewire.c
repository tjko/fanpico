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



typedef struct onewire_state_t {
	pico_1wire_t *ctx;
	uint devices;
	uint64_t addr[ONEWIRE_MAX_COUNT];
	float temp[ONEWIRE_MAX_COUNT];
} onewire_state_t;

static onewire_state_t onewire_state_struct;
static onewire_state_t *onewire_bus = &onewire_state_struct;


uint64_t onewire_address(uint sensor)
{
	if (sensor >= onewire_bus->devices)
		return 0;

	return onewire_bus->addr[sensor];
}


void onewire_scan_bus()
{
	int res, retries;
	bool psu;
	uint count;

	if (!onewire_bus->ctx)
		return;

	log_msg(LOG_INFO, "Scanning 1-Wire bus...");

	/* Check for any devices on the bus... */
	retries = 0;
	do {
		res = pico_1wire_reset_bus(onewire_bus->ctx);
	} while (!res && retries++ < 3);
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
				ONEWIRE_MAX_COUNT, &count);
	if (res == 2) {
		log_msg(LOG_NOTICE, "1-Wire bus has more than %d devices.",
			ONEWIRE_MAX_COUNT);
	} else 	if (res) {
		log_msg(LOG_NOTICE, "1-Wire Search ROM Addresses failed: %d", res);
		return;
	}

	if (onewire_bus->devices != count) {
		log_msg(LOG_INFO, "1-Wire device count change detected: %u --> %u",
			onewire_bus->devices, count);
		for (int i = 0; i < count; i++) {
			log_msg(LOG_INFO, "1-Wire Device%d: %016llx",
				i + 1, onewire_bus->addr[i]);
		}
		onewire_bus->devices = count;
	}
}

int onewire_read_temps(struct fanpico_config *config, struct fanpico_state *state)
{
	static uint step = 0;
	static uint sensor = 0;
	int res;
	float temp;

	if (!onewire_bus->ctx)
		return -1;

	if (step > 1)
		step = 0;
	if (onewire_bus->devices < 1)
		step = 2;

	if (step == 0) {
		/* Send "Convert Temperature" command to all devices. */
		res = pico_1wire_convert_temperature(onewire_bus->ctx, 0, false);
		if (res) {
			log_msg(LOG_INFO, "pico_1wire_convert_temperature() failed: %d", res);
			step = 2;
		} else {
			log_msg(LOG_DEBUG, "1-Wire Initiate temperature conversion");
			sensor = 0;
			step++;
		}
		return 1000; /* delay 1sec before reading measurement results (min 750ms) */
	}
	else if (step == 1) {
		if (sensor < onewire_bus->devices) {
			/* Read Temperature from one sensor */
			res = pico_1wire_get_temperature(onewire_bus->ctx, onewire_bus->addr[sensor], &temp);
			if (res) {
				log_msg(LOG_INFO, "1-Wire Device%d: cannot get temperature: %d",
					sensor + 1, res);
			} else {
				log_msg(LOG_DEBUG, "1-Wire Device%d: temperature %5.1fC",
					sensor + 1, temp);
				state->onewire_temp[sensor] = temp;
				state->onewire_temp_updated[sensor] = get_absolute_time();
			}
			sensor++;
			return 100; /* delay 100ms between reading sensors */
		}
		step = 2;
	}

	return 15000; /* delay 15sec between measurements */
}


void setup_onewire_bus()
{
	memset(onewire_bus, 0, sizeof(onewire_state_t));

	if (!cfg->onewire_active) {
		log_msg(LOG_INFO, "1-Wire Bus disabled");
		return;
	}
	else if ((!cfg->spi_active && !cfg->serial_active) || !ONEWIRE_SHARED) {
		log_msg(LOG_NOTICE, "Initializing 1-Wire Bus...");

		onewire_bus->ctx = pico_1wire_init(ONEWIRE_PIN, -1, true);
		if (!onewire_bus->ctx) {
			log_msg(LOG_ERR, "1-Wire intialization failed!");
			return;
		}
		onewire_scan_bus();
	}
}
