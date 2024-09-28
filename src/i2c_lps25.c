/* i2c_lps25.c
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

/* LPS25 Registers */
#define WHO_AM_I         0x0f
#define RES_CONF         0x10
#define CTRL_REG1        0x20
#define CTRL_REG2        0x21
#define CTRL_REG3        0x22
#define CTRL_REG4        0x23
#define STATUS_REG       0x27
#define PRES_OUT_XL      0x28
#define PRES_OUT_L       0x29
#define PRES_OUT_H       0x2a
#define TEMP_OUT_L       0x2b
#define TEMP_OUT_H       0x2c
#define FIFO_CTRL        0x2e
#define FIFO_STATUS      0x2f

#define LPS25_DEVICE_ID      0xbd


typedef struct lps25_context_t {
	i2c_inst_t *i2c;
	uint8_t addr;
} lps25_context_t;


void* lps25_init(i2c_inst_t *i2c, uint8_t addr)
{
	int res;
	uint8_t val = 0;
	lps25_context_t *ctx = calloc(1, sizeof(lps25_context_t));

	if (!ctx)
		return NULL;
	ctx->i2c = i2c;
	ctx->addr = addr;

	/* Read and verify device ID */
	res  = i2c_read_register_u8(i2c, addr, WHO_AM_I, &val);
	if (res || val != LPS25_DEVICE_ID)
		goto panic;

	/* Reset Sensor */
	res = i2c_write_register_u8(i2c, addr, CTRL_REG2, 0x04); // SWRESET
	if (res)
		goto panic;
	sleep_us(5);
	res = i2c_write_register_u8(i2c, addr, CTRL_REG2, 0x80); // BOOT
	if (res)
		goto panic;
	sleep_us(2300);

	/* Read configuration register */
	res = i2c_read_register_u8(i2c, addr, CTRL_REG2, &val);
	if (res)
		goto panic;
	/* Check that boot is complete */
	if (val & 0x80)
		goto panic;

	/* Set Pressure and temperature resolution */
	res = i2c_write_register_u8(i2c, addr, RES_CONF, 0x0f);
	if (res)
		goto panic;

	/* Set active mode and output data rate (12.5Hz) */
	res = i2c_write_register_u8(i2c, addr, CTRL_REG1, 0xb0);
	if (res)
		goto panic;

	/* Set FIFO to Mean mode (32-sample moving average) */
	res = i2c_write_register_u8(i2c, addr, FIFO_CTRL, 0xdf);
	if (res)
		goto panic;

	return ctx;

panic:
	free(ctx);
	return NULL;
}


int lps25_start_measurement(void *ctx)
{
	/* Nothing to do, sensor is in continuous measurement mode... */

	return 1000;  /* measurement should be available after 1s */
}


int lps25_get_measurement(void *ctx, float *temp, float *pressure, float *humidity)
{
	lps25_context_t *c = (lps25_context_t*)ctx;
	int res;
	uint8_t val;
	uint8_t buf[5];
	int32_t t_raw, p_raw;


	/* Read status register */
	res = i2c_read_register_u8(c->i2c, c->addr, STATUS_REG, &val);
	if (res)
		return -1;

	/* Check P_DA and T_DA (data available) bits */
	if ((val & 0x03) != 0x03)
		return 1;

	/* Get Measurement */
	res = i2c_read_register_block(c->i2c, c->addr, PRES_OUT_XL | 0x80, buf, sizeof(buf), 0);
	if (res)
		return -2;

	p_raw = twos_complement((uint32_t)((buf[2] << 16) | (buf[1] << 8) | buf[0]), 24);
	t_raw = twos_complement((uint32_t)(((buf[4] << 8) | buf[3])), 16);

	DEBUG_PRINT("t_raw = %ld, p_raw = %ld\n", t_raw, p_raw);

	*temp = 42.5 + t_raw / 480.0;
	*pressure = p_raw / 4096.0;
	*humidity = -1.0;

	DEBUG_PRINT("temp = %0.1f C, pressure = %0.1f hPa\n", *temp, *pressure);

	return 0;
}


