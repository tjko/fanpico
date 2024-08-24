/* i2c_as621x.c
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
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#include "i2c.h"

/* AS621x (AS6212/AS6214/AS6128) Registers */
#define REG_TVAL         0x00
#define REG_CONFIG       0x01
#define REG_T_LOW        0x02
#define REG_T_HIGH       0x03


#define AS621X_DEVICE_ID 0x117

typedef struct as621x_context_t {
	i2c_inst_t *i2c;
	uint8_t addr;
} as621x_context_t;


void* as621x_init(i2c_inst_t *i2c, uint8_t addr)
{
	int res;
	uint16_t val = 0;
	int16_t temp;
	as621x_context_t *ctx = calloc(1, sizeof(as621x_context_t));

	if (!ctx)
		return NULL;
	ctx->i2c = i2c;
	ctx->addr = addr;


	/* Check that configuration register reserved bits are set as expected for AS621x */

	res = i2c_read_register_u16(i2c, addr, REG_CONFIG, &val);
	if (res || (val & 0x601f) != 0x4000)
		goto panic;


	/* Try to detect device by T_HIGH and T_LOW register default values (80C and 75C) */

	res  = i2c_read_register_u16(i2c, addr, REG_T_HIGH, &val);
	if (res)
		goto panic;
	temp = (int16_t)val / 128;
	DEBUG_PRINT("T_high = %d\n", temp);
	if (temp != 80)
		goto panic;

	res  = i2c_read_register_u16(i2c, addr, REG_T_LOW, &val);
	if (res)
		goto panic;
	temp = (int16_t)val / 128;
	DEBUG_PRINT("T_low = %d\n", temp);
	if (temp != 75)
		goto panic;


	/* Set sensor configuration: continuous measruements at 4Hz */
	res = i2c_write_register_u16(i2c, addr, REG_CONFIG, 0x00a0);
	if (res)
		goto panic;

	sleep_us(100);

	/* Read configuration register */
	res = i2c_read_register_u16(i2c, addr, REG_CONFIG, &val);
	if (res)
		goto panic;

	/* Check that confuration is now as expected (excluding read-only reserved bits)... */
	if ((val & 0x9fe0) != 0x00a0)
		goto panic;

	return ctx;

panic:
	free(ctx);
	return NULL;
}


int as621x_start_measurement(void *ctx)
{
	/* Nothing to do, sensor is in continuous measurement mode... */

	return 250;  /* measurement should be available after 250ms */
}


int as621x_get_measurement(void *ctx, float *temp, float *pressure, float *humidity)
{
	as621x_context_t *c = (as621x_context_t*)ctx;
	int res;
	uint16_t val;


	/* Get Measurement */
	res = i2c_read_register_u16(c->i2c, c->addr, REG_TVAL, &val);
	if (res)
		return -2;

	*temp = ((int16_t)val) / 128.0;
	DEBUG_PRINT("t_raw = %d, temp=%0.2f\n", val, *temp);

	*pressure = -1.0;
	*humidity = -1.0;

	return 0;
}


