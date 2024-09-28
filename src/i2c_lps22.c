/* i2c_lps22.c
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

/* LPS22 Registers */
#define INTERRUPT_CFG    0x0b
#define THS_P_L          0x0c
#define THS_P_H          0x0d
#define IF_CTRL          0x0e
#define WHO_AM_I         0x0f
#define CTRL_REG1        0x10
#define CTRL_REG2        0x11
#define CTRL_REG3        0x12
#define FIFO_CTRL        0x13
#define FIFO_WTM         0x14
#define REF_P_L          0x15
#define REF_P_H          0x16
#define RPDS_L           0x18
#define RPDS_H           0x19
#define INT_SOURCE       0x24
#define FIFO_STATUS1     0x25
#define FIFO_STATUS2     0x26
#define STATUS           0x27
#define PRES_OUT_XL      0x28
#define PRES_OUT_L       0x29
#define PRES_OUT_H       0x2a
#define TEMP_OUT_L       0x2b
#define TEMP_OUT_H       0x2c
#define FIFO_DATA_OUT_PRESS_XL 0x78
#define FIFO_DATA_OUT_PRESS_L  0x79
#define FIFO_DATA_OUT_PRESS_H  0x7a
#define FIFO_DATA_OUT_TEMP_L   0x7b
#define FIFO_DATA_OUT_TEMP_H   0x7c

#define LPS22_DEVICE_ID      0xb1


typedef struct lps22_context_t {
	i2c_inst_t *i2c;
	uint8_t addr;
} lps22_context_t;


void* lps22_init(i2c_inst_t *i2c, uint8_t addr)
{
	int res;
	uint8_t val = 0;
	lps22_context_t *ctx = calloc(1, sizeof(lps22_context_t));

	if (!ctx)
		return NULL;
	ctx->i2c = i2c;
	ctx->addr = addr;

	/* Read and verify device ID */
	res  = i2c_read_register_u8(i2c, addr, WHO_AM_I, &val);
	if (res || val != LPS22_DEVICE_ID)
		goto panic;

	/* Reset Sensor */
	res = i2c_write_register_u8(i2c, addr, CTRL_REG2, 0x04); // SWRESET
	if (res)
		goto panic;
	sleep_us(5);
	res = i2c_write_register_u8(i2c, addr, CTRL_REG2, 0x92); // BOOT
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


	/* Set continuous mode and output data rate (25Hz) */
	res = i2c_write_register_u8(i2c, addr, CTRL_REG1, 0x3c);
	if (res)
		goto panic;

	/* Set FIFO to Continuous mode */
	res = i2c_write_register_u8(i2c, addr, FIFO_CTRL, 0x03);
	if (res)
		goto panic;

	return ctx;

panic:
	free(ctx);
	return NULL;
}


int lps22_start_measurement(void *ctx)
{
	/* Nothing to do, sensor is in continuous measurement mode... */

	return 1000;  /* measurement should be available after 1s */
}


int lps22_get_measurement(void *ctx, float *temp, float *pressure, float *humidity)
{
	lps22_context_t *c = (lps22_context_t*)ctx;
	int res;
	uint8_t val;
	uint8_t buf[5];
	int32_t t_raw, p_raw;


	/* Read status register */
	res = i2c_read_register_u8(c->i2c, c->addr, STATUS, &val);
	if (res)
		return -1;

	/* Check P_DA and T_DA (data available) bits */
	if ((val & 0x03) != 0x03)
		return 1;

	/* Get Measurement */
	res = i2c_read_register_block(c->i2c, c->addr, PRES_OUT_XL, buf, sizeof(buf), 0);
	if (res)
		return -2;

	p_raw = twos_complement((uint32_t)((buf[2] << 16) | (buf[1] << 8) | buf[0]), 24);
	t_raw = twos_complement((uint32_t)(((buf[4] << 8) | buf[3])), 16);

	DEBUG_PRINT("t_raw = %ld, p_raw = %ld\n", t_raw, p_raw);

	*temp = t_raw / 100.0;
	*pressure = p_raw / 4096.0;
	*humidity = -1.0;

	DEBUG_PRINT("temp = %0.1f C, pressure = %0.1f hPa\n", *temp, *pressure);

	return 0;
}


