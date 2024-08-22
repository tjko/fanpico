/* i2c_tmp117.c
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

/* TMP117 Registers */
#define TMP117_REG_TEMP_RESULT  0x00
#define TMP117_REG_CONFIG       0x01
#define TMP117_REG_DEVICE_ID    0x0f

#define TMP117_DEVICE_ID 0x117

typedef struct tmp117_context_t {
	i2c_inst_t *i2c;
	uint8_t addr;
} tmp117_context_t;


void* tmp117_init(i2c_inst_t *i2c, uint8_t addr)
{
	int res;
	uint16_t val = 0;
	tmp117_context_t *ctx = calloc(1, sizeof(tmp117_context_t));

	if (!ctx)
		return NULL;
	ctx->i2c = i2c;
	ctx->addr = addr;

	/* Read and verify device ID */
	res  = i2c_read_register_u16(i2c, addr, TMP117_REG_DEVICE_ID, &val);
	if (res || val != TMP117_DEVICE_ID)
		goto panic;

	/* Reset Sensor */
	res = i2c_write_register_u16(i2c, addr, TMP117_REG_CONFIG, 0x0222);
	if (res)
		goto panic;

	/* Wait for sensor to soft reset (reset should take 2ms per datasheet)  */
	sleep_us(2500);

	/* Read configuration register */
	res = i2c_read_register_u16(i2c, addr, TMP117_REG_CONFIG, &val);
	if (res)
		goto panic;

	/* Check that confuration is now as expected... */
	if ((val & 0x0FFC) != 0x0220)
		goto panic;

	return ctx;

panic:
	free(ctx);
	return NULL;
}


int tmp117_start_measurement(void *ctx)
{
	/* Nothing to do, sensor is in continuous measurement mode... */

	return 1000;  /* measurement should be available after 1s */
}


int tmp117_get_measurement(void *ctx, float *temp, float *pressure, float *humidity)
{
	tmp117_context_t *c = (tmp117_context_t*)ctx;
	int res;
	uint16_t val;


	/* Read configuration register */
	res = i2c_read_register_u16(c->i2c, c->addr, TMP117_REG_CONFIG, &val);
	if (res)
		return -1;

	/* Check Data_Ready bit */
	if ((val & 0x2000) == 0)
		return 1;

	/* Get Measurement */
	res = i2c_read_register_u16(c->i2c, c->addr, TMP117_REG_TEMP_RESULT, &val);
	if (res)
		return -2;

	*temp = ((int16_t)val) / 128.0;
	*pressure = -1.0;
	*humidity = -1.0;

	return 0;
}


