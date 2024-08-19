/* i2c_dps310.c
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

/* DPS310 Registers */
#define REG_PSR_B2        0x00
#define REG_PSR_B1        0x01
#define REG_PST_B0        0x02
#define REG_TMP_B2        0x03
#define REG_TMP_B1        0x04
#define REG_TMP_B0        0x05
#define REG_PRS_CFG       0x06
#define REG_TMP_CFG       0x07
#define REG_MEAS_CFG      0x08
#define REG_CFG           0x09
#define REG_INT_STS       0x0a
#define REG_FIFO_STS      0x0b
#define REG_RESET         0x0c
#define REG_ID            0x0d
#define REG_COEF          0x10 // 0x10 - 0x21 (18 bytes to read)
#define REG_COEF_SRCE     0x28


#define DPS310_DEVICE_ID  0x10  // revision id [7:4], product id [3:0]


typedef struct dps310_context_t {
	i2c_inst_t *i2c;
	uint8_t addr;
	// Calibration Coefficients
	int16_t c0;
	int16_t c1;
	int32_t c00;
	int32_t c10;
	int16_t c01;
	int16_t c11;
	int16_t c20;
	int16_t c21;
	int16_t c30;
} dps310_context_t;



void* dps310_init(i2c_inst_t *i2c, uint8_t addr)
{
	int res;
	uint8_t val = 0;
	uint8_t buf[18];
	uint8_t coef_source;
	dps310_context_t *ctx = calloc(1, sizeof(dps310_context_t));


	if (!ctx)
		return NULL;
	ctx->i2c = i2c;
	ctx->addr = addr;

	/* Read and verify device ID */
	res  = i2c_read_register_u8(i2c, addr, REG_ID, &val);
	if (res || (val & 0x0f) != (DPS310_DEVICE_ID & 0x0f))
		goto panic;
	/* Revision ID should greater than zero... */
	if (val >> 4 == 0)
		goto panic;


	/* Reset Sensor */
	res = i2c_write_register_u8(i2c, addr, REG_RESET, 0x89);
	if (res)
		goto panic;

	/* Wait for sensor to soft reset (reset should take 40ms per datasheet)  */
	sleep_ms(40);

	/* Get coefficient source */
	res = i2c_read_register_u8(i2c, addr, REG_COEF_SRCE, &val);
	if (res)
		goto panic;
	coef_source = (val >> 7);


	/* Write configuration registers */

	// 4 pressure measruementes per second, 64 times oversampling
	res = i2c_write_register_u8(i2c, addr, REG_PRS_CFG, 0x26);
	if (res)
		goto panic;
	// 4 temp measurements per second, 64 times oversampling
	res = i2c_write_register_u8(i2c, addr, REG_TMP_CFG, (coef_source << 7) | 0x26);
	if (res)
		goto panic;
	// enable continuous pressure and temperature measurement
	res = i2c_write_register_u8(i2c, addr, REG_MEAS_CFG, 0x07);
	if (res)
		goto panic;
	// enable pressure result bit-shift and temperature result bit-shift
	res = i2c_write_register_u8(i2c, addr, REG_CFG, 0x0c);
	if (res)
		goto panic;


	/* Get sensor status and check COEF_RDY bit */
	res  = i2c_read_register_u8(i2c, addr, REG_MEAS_CFG, &val);
	if (res || (val & 0x80) == 0)
		goto panic;

	/* Read calibration coefficients */
	res = i2c_read_register_block(i2c, addr, REG_COEF, buf, 18);
	if (res)
		goto panic;

	ctx->c0 = twos_complement((buf[0] << 4) | (buf[1] >> 4), 12);
	ctx->c1 = twos_complement(((buf[1] & 0x0f) << 8) | (buf[2]), 12);
	ctx->c00 = twos_complement((buf[3] << 12) | (buf[4] << 4) | (buf[5] >> 4), 20);
	ctx->c10 = twos_complement(((buf[5] & 0x0f) << 16) | (buf[6] << 8) | buf[7], 20);
	ctx->c01 = twos_complement((buf[8] << 8) | buf[9], 16);
	ctx->c11 = twos_complement((buf[10] << 8) | buf[11], 16);
	ctx->c20 = twos_complement((buf[12] << 8) | buf[13], 16);
	ctx->c21 = twos_complement((buf[14] << 8) | buf[15], 16);
	ctx->c30 = twos_complement((buf[16] << 8) | buf[17], 16);

	return ctx;

panic:
	free(ctx);
	return NULL;
}


int dps310_start_measurement(void *ctx)
{
	/* Nothing to do, sensor is in continuous measurement mode... */

	return 1000;  /* measurement should be available after 1s */
}


int dps310_get_measurement(void *ctx, float *temp)
{
	dps310_context_t *c = (dps310_context_t*)ctx;
	int res;
	uint8_t status;
	uint32_t meas;


	/* Get sensor status */
	res  = i2c_read_register_u8(c->i2c, c->addr, REG_MEAS_CFG, &status);
	if (res)
		return -1;

	/* Get Temperature Measurement if TMP_RDY bit set */
	if ((status & 0x20)) {
		res = i2c_read_register_u24(c->i2c, c->addr, REG_TMP_B2, &meas);
		if (res)
			return -2;

		*temp =  (double)meas / 1040384 * c->c1 + c->c0 * 0.5;
	} else {
		return -3;
	}


	return 0;
}
