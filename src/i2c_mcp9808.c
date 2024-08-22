/* i2c_mcp9808.c
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

/* MCP9808 Registers */
#define REG_CONFIG        0x01
#define REG_T_UPPER       0x02
#define REG_T_LOWER       0x03
#define REG_T_CRIT        0x04
#define REG_T_A           0x05
#define REG_MANUF_ID      0x06
#define REG_DEVICE_ID     0x07
#define REG_RESOLUTION    0x08

#define MCP9808_MANUF_ID   0x0054
#define MCP9808_DEVICE_ID  0x04


typedef struct mcp9808_context_t {
	i2c_inst_t *i2c;
	uint8_t addr;
} mcp9808_context_t;



void* mcp9808_init(i2c_inst_t *i2c, uint8_t addr)
{
	int res;
	uint16_t val = 0;
	mcp9808_context_t *ctx = calloc(1, sizeof(mcp9808_context_t));


	if (!ctx)
		return NULL;
	ctx->i2c = i2c;
	ctx->addr = addr;

	/* Read and verify manufacturer ID */
	res  = i2c_read_register_u16(i2c, addr, REG_MANUF_ID, &val);
	if (res || (val != MCP9808_MANUF_ID))
		goto panic;

	/* Read and verify device ID */
	res  = i2c_read_register_u16(i2c, addr, REG_DEVICE_ID, &val);
	if (res || ((val >> 8) != MCP9808_DEVICE_ID))
		goto panic;


	/* Set sensor configuration */
	res = i2c_write_register_u16(i2c, addr, REG_CONFIG, 0x0000);
	if (res)
		goto panic;

	/* Set resolution */
	res = i2c_write_register_u8(i2c, addr, REG_CONFIG, 0x03);
	if (res)
		goto panic;


	return ctx;

panic:
	free(ctx);
	return NULL;
}


int mcp9808_start_measurement(void *ctx)
{
	/* Nothing to do, sensor is in continuous measurement mode... */

	return 250;  /* measurement should be available after 250ms (4 measurements/sec) */
}


int mcp9808_get_measurement(void *ctx, float *temp, float *pressure, float *humidity)
{
	mcp9808_context_t *c = (mcp9808_context_t*)ctx;
	uint16_t meas = 0;
	int res;


	/* Get Temperature Measurement */
	res = i2c_read_register_u16(c->i2c, c->addr, REG_T_A, &meas);
	if (res)
		return -1;

	*temp = (double)twos_complement(meas, 13) / 16.0;
	*pressure = -1.0;
	*humidity = -1.0;

	return 0;
}
