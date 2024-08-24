/* i2c_tmp102.c
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

/* TMP102 Registers */
#define REG_TEMP_RESULT  0x00
#define REG_CONFIG       0x01
#define REG_T_LOW        0x02
#define REG_T_HIGH       0x03



typedef struct tmp102_context_t {
	i2c_inst_t *i2c;
	uint8_t addr;
} tmp102_context_t;


void* tmp102_init(i2c_inst_t *i2c, uint8_t addr)
{
	int res;
	uint16_t val = 0;
	tmp102_context_t *ctx = calloc(1, sizeof(tmp102_context_t));

	if (!ctx)
		return NULL;
	ctx->i2c = i2c;
	ctx->addr = addr;


	/* Verify configuration register read-only bits */
	res  = i2c_read_register_u16(i2c, addr, REG_CONFIG, &val);
	if (res || ((val & 0x600f) != 0x6000))
		goto panic;

	/* Try to detect device by T_HIGH and T_LOW register default values (80C and 75C) */

	res  = i2c_read_register_u16(i2c, addr, REG_T_HIGH, &val);
	if (res)
		goto panic;
	val = twos_complement((val >> 4), 12) / 16;
	DEBUG_PRINT("T_high = %d\n", val);
	if (val != 80)
		goto panic;

	res  = i2c_read_register_u16(i2c, addr, REG_T_LOW, &val);
	if (res)
		goto panic;
	val = twos_complement((val >> 4), 12) / 16;
	DEBUG_PRINT("T_low = %d\n", val);
	if (val != 75)
		goto panic;


	/* Set sensor configuration: enable EM mode */
	res = i2c_write_register_u16(i2c, addr, REG_CONFIG, 0x00b0);
	if (res)
		goto panic;

	sleep_us(100);

	/* Read configuration register */
	res = i2c_read_register_u16(i2c, addr, REG_CONFIG, &val);
	if (res)
		goto panic;

	/* Check that confuration is now as expected (excluding read-only bits R1/R0)... */
	if ((val & 0x9fff) != 0x00b0)
		goto panic;

	return ctx;

panic:
	free(ctx);
	return NULL;
}


int tmp102_start_measurement(void *ctx)
{
	/* Nothing to do, sensor is in continuous measurement mode... */

	return 250;  /* measurement should be available after 250ms */
}


int tmp102_get_measurement(void *ctx, float *temp, float *pressure, float *humidity)
{
	tmp102_context_t *c = (tmp102_context_t*)ctx;
	int res;
	uint16_t val;


	/* Get Measurement */
	res = i2c_read_register_u16(c->i2c, c->addr, REG_TEMP_RESULT, &val);
	if (res)
		return -2;

	val = twos_complement((val >> 3), 13);
	*temp = ((int16_t)val) / 16.0;
	DEBUG_PRINT("t_raw = %d, temp=%0.2f\n", val, *temp);

	*pressure = -1.0;
	*humidity = -1.0;

	return 0;
}


