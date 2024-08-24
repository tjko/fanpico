/* i2c_stts22h.c
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

/* STTS22H Registers */
#define REG_DEVICE_ID      0x01
#define REG_TEMP_H_LIMIT   0x02
#define REG_TEMP_L_LIMIT   0x03
#define REG_CTRL           0x04
#define REG_STATUS         0x05
#define REG_TEMP_L_OUT     0x06
#define REG_TEMP_H_OUT     0x07

#define STTS22H_DEVICE_ID 0xa0

typedef struct stts22h_context_t {
	i2c_inst_t *i2c;
	uint8_t addr;
} stts22h_context_t;


void* stts22h_init(i2c_inst_t *i2c, uint8_t addr)
{
	int res;
	uint8_t val = 0;
	stts22h_context_t *ctx = calloc(1, sizeof(stts22h_context_t));

	if (!ctx)
		return NULL;
	ctx->i2c = i2c;
	ctx->addr = addr;

	/* Read and verify device ID */
	res  = i2c_read_register_u8(i2c, addr, REG_DEVICE_ID, &val);
	if (res || val != STTS22H_DEVICE_ID)
		goto panic;

	/* Configure Sensor: set to defaults first */
	res = i2c_write_register_u8(i2c, addr, REG_CTRL, 0x00);
	if (res)
		goto panic;

	sleep_us(10);

	/* Configure Sensor: avg=00, if_add_inc=1, freerun=1 */
	res = i2c_write_register_u8(i2c, addr, REG_CTRL, 0x0c);
	if (res)
		goto panic;

	/* Read and check configuration register */
	res = i2c_read_register_u8(i2c, addr, REG_CTRL, &val);
	if (res | (val != 0x0c))
		goto panic;

	return ctx;

panic:
	free(ctx);
	return NULL;
}


int stts22h_start_measurement(void *ctx)
{
	/* Nothing to do, sensor is in continuous measurement mode... */

	return 50;  /* measurement should be available after 40ms (25Hz) */
}


int stts22h_get_measurement(void *ctx, float *temp, float *pressure, float *humidity)
{
	stts22h_context_t *c = (stts22h_context_t*)ctx;
	int res;
	uint16_t val;


	/* Get Measurement */
	res = i2c_read_register_u16(c->i2c, c->addr, REG_TEMP_L_OUT, &val);
	if (res)
		return -2;

	val = (val << 8) | (val >> 8); /* reverse byte order */
	*temp = ((int16_t)val) / 100.0;
	*pressure = -1.0;
	*humidity = -1.0;

	DEBUG_PRINT("raw_temp=%d, temp=%0.2f\n", val, *temp);

	return 0;
}


