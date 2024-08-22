/* i2c_pct2075.c
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

/* PCT2075 Registers */
#define REG_TEMP          0x00
#define REG_CONFIG        0x01
#define REG_T_HYST        0x02
#define REG_T_OS          0x03
#define REG_T_IDLE        0x04


typedef struct pct2075_context_t {
	i2c_inst_t *i2c;
	uint8_t addr;
} pct2075_context_t;



void* pct2075_init(i2c_inst_t *i2c, uint8_t addr)
{
	int res;
	uint8_t cfg = 0;
	pct2075_context_t *ctx = calloc(1, sizeof(pct2075_context_t));


	if (!ctx)
		return NULL;
	ctx->i2c = i2c;
	ctx->addr = addr;

	/* Read config register */
	res  = i2c_read_register_u8(i2c, addr, REG_CONFIG, &cfg);
	if (res)
		goto panic;

	/* High 3bits should always be zero */
	if ((cfg & 0xe0) != 0)
		goto panic;

	/* Read T_idle register */
	res  = i2c_read_register_u8(i2c, addr, REG_T_IDLE, &cfg);
	if (res)
		goto panic;

	/* T_idle should default to 1 */
	if ((cfg & 0x1f) != 1)
		goto panic;


	/* Set configuration (to defaults) */
	res = i2c_write_register_u8(i2c, addr, REG_CONFIG, 0x00);
	if (res)
		goto panic;

	/* Set sampling period to 100ms */
	res = i2c_write_register_u8(i2c, addr, REG_T_IDLE, 0x01);
	if (res)
		goto panic;


	return ctx;

panic:
	free(ctx);
	return NULL;
}


int pct2075_start_measurement(void *ctx)
{
	/* Nothing to do, sensor is in continuous measurement mode... */

	return 100;  /* measurement should be available after 250ms (4 measurements/sec) */
}


int pct2075_get_measurement(void *ctx, float *temp, float *pressure, float *humidity)
{
	pct2075_context_t *c = (pct2075_context_t*)ctx;
	uint16_t meas = 0;
	int res;


	/* Get Temperature Measurement */
	res = i2c_read_register_u16(c->i2c, c->addr, REG_TEMP, &meas);
	if (res)
		return -1;

	meas >>= 5;
	*temp = (double)twos_complement(meas, 11) / 8.0;
	*pressure = -1.0;
	*humidity = -1.0;

	return 0;
}
