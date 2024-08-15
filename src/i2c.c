/* i2c.c
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
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#include "fanpico.h"


static bool i2c_bus_active = false;
static i2c_inst_t *i2c_bus = NULL;


static bool i2c_reserved_address(uint8_t addr)
{
	return (addr & 0x78) == 0 || (addr & 0x78) == 0x78;
}



void scan_i2c_bus()
{
	int res;
	uint8_t buf[2];
	int found = 0;

	if (!i2c_bus_active)
		return;

	printf("Scanning I2C Bus... ");

	for (uint addr = 0; addr < 0x80; addr++) {
		if (i2c_reserved_address(addr))
			continue;
		res = i2c_read_blocking(i2c_bus, addr, buf, 1, false);
		if (res < 0)
			continue;
		if (found > 0)
			printf(", ");
		printf("0x%02x", addr);
		found++;
	}
	printf("\nDevice(s) found: %d\n", found);
}



void display_i2c_status()
{
	printf("%d\n", i2c_bus_active ? 1 : 0);
}


void setup_i2c_bus()
{
	uint speed;

	i2c_bus_active = false;
#if SDA_PIN >= 0
	if (!SPI_SHARED || !cfg->spi_active)
		i2c_bus_active = true;
#endif

	if (!i2c_bus_active) {
		log_msg(LOG_INFO, "I2C Bus disabled");
		return;
	}

	log_msg(LOG_INFO, "Initializing I2C Bus...",
		cfg->i2c_speed / 1000);

	i2c_bus = (I2C_HW > 1 ? i2c1 : i2c0);
	speed = i2c_init(i2c_bus, cfg->i2c_speed);
	log_msg(LOG_INFO, "I2C Bus initalized at %u kHz", speed / 1000);
	gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
	gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
	gpio_pull_up(SDA_PIN);
	gpio_pull_up(SCL_PIN);
}
