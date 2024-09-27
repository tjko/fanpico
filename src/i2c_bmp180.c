/* i2c_bmp180.c
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

/* BMP180 Registers */
#define REG_CALIB         0xaa // 0xaa to 0xbf
#define REG_ID            0xd0
#define REG_RESET         0xe0 // write 0xb6 to reset
#define REG_CTRL_MEAS     0xf4
#define REG_OUT_MSB       0xf6
#define REG_OUT_LSB       0xf7
#define REG_OUT_XLSB      0xf8


#define BMP180_DEVICE_ID  0x55


typedef struct bmp180_context_t {
	i2c_inst_t *i2c;
	uint8_t addr;
	uint8_t state;
	int32_t temp;
	int32_t pressure;
	// Calibration Data
	int16_t ac1;
	int16_t ac2;
	int16_t ac3;
	uint16_t ac4;
	uint16_t ac5;
	uint16_t ac6;
	int16_t b1;
	int16_t b2;
	int16_t mb;
	int16_t mc;
	int16_t md;
	// Calculation vars
	int32_t x1;
	int32_t x2;
	int32_t b5;
} bmp180_context_t;



void* bmp180_init(i2c_inst_t *i2c, uint8_t addr)
{
	bmp180_context_t *ctx = calloc(1, sizeof(bmp180_context_t));
	uint8_t buf[22];
	uint8_t val = 0;
	int res;

	if (!ctx)
		return NULL;
	ctx->i2c = i2c;
	ctx->addr = addr;
	ctx->state = 0;
	ctx->temp = 0;
	ctx->pressure = -1;

	/* Read and verify device ID */
	res  = i2c_read_register_u8(i2c, addr, REG_ID, &val);
	if (res || (val  != BMP180_DEVICE_ID))
		goto panic;

	/* Reset Sensor */
	res = i2c_write_register_u8(i2c, addr, REG_RESET, 0xb6);
	if (res)
		goto panic;

	/* Wait for sensor to soft reset (reset should take 2ms per datasheet)  */
	sleep_ms(10);


	/* Read calibration data */
	res = i2c_read_register_block(i2c, addr, REG_CALIB, buf, 22, 0);
	if (res)
		goto panic;

	ctx->ac1 = (buf[0] << 8) | buf[1];
	ctx->ac2 = (buf[2] << 8) | buf[3];
	ctx->ac3 = (buf[4] << 8) | buf[5];
	ctx->ac4 = (buf[6] << 8) | buf[7];
	ctx->ac5 = (buf[8] << 8) | buf[9];
	ctx->ac6 = (buf[10] << 8) | buf[11];
	DEBUG_PRINT("ac1=%04x %d\n", ctx->ac1, ctx->ac1);
	DEBUG_PRINT("ac2=%04x %d\n", ctx->ac2, ctx->ac2);
	DEBUG_PRINT("ac3=%04x %d\n", ctx->ac3, ctx->ac3);
	DEBUG_PRINT("ac4=%04x %u\n", ctx->ac4, ctx->ac4);
	DEBUG_PRINT("ac5=%04x %u\n", ctx->ac5, ctx->ac5);
	DEBUG_PRINT("ac6=%04x %u\n", ctx->ac6, ctx->ac6);

	ctx->b1 = (buf[12] << 8) | buf[13];
	ctx->b2 = (buf[14] << 8) | buf[15];
	ctx->mb = (buf[16] << 8) | buf[17];
	ctx->mc = (buf[18] << 8) | buf[19];
	ctx->md = (buf[20] << 8) | buf[21];
	DEBUG_PRINT("b1=%04x %d\n", ctx->b1, ctx->b1);
	DEBUG_PRINT("b2=%04x %d\n", ctx->b2, ctx->b2);
	DEBUG_PRINT("mb=%04x %d\n", ctx->mb, ctx->mb);
	DEBUG_PRINT("mc=%04x %d\n", ctx->mc, ctx->mc);
	DEBUG_PRINT("md=%04x %d\n", ctx->md, ctx->md);

	return ctx;

panic:
	free(ctx);
	return NULL;
}


int bmp180_start_measurement(void *ctx)
{
	bmp180_context_t *c = (bmp180_context_t*)ctx;
	uint8_t val;
	int t;

	if (c->state == 0) {
		/* measure temp */
		val = 0x2e;
		t = 10;
	} else {
		val = 0xf4;
		t = 30;
	}
	i2c_write_register_u8(c->i2c, c->addr, REG_CTRL_MEAS, val);

	return t;
}


/* Calculactions based on datasheet example */
static int32_t bmp180_compensated_t(int32_t ut, bmp180_context_t *c)
{
	int32_t t;

	c->x1 = (ut - (uint32_t)c->ac6) * (uint32_t)c->ac5 / 32768;
	c->x2 = (int32_t)c->mc * 2048 / (c->x1 + c->md);
	c->b5 = c->x1 + c->x2;
	t = (c->b5 + 8) / 16;

	return t;
}


/* Calculactions based on datasheet example */
static int32_t bmp180_compensated_p(int32_t up, bmp180_context_t *c)
{
	int32_t b3, b6, x1, x2, x3, p;
	uint32_t b4, b7;

	b6 = c->b5 - 4000;
	x1 = ((int32_t)c->b2 * (b6 * b6 / 4096)) / 2048;
	x2 = (int32_t)c->ac2 * b6 / 2048;
	x3 = x1 + x2;
	b3 = ((((int32_t)c->ac1 * 4 + x3) << 3) + 2) / 4;
	x1 = (int32_t)c->ac3 * b6 / 8192;
	x2 = ((int32_t)c->b1 * (b6 * b6 / 4096)) / 32768;
	x3 = ((x1 + x2) + 2) / 4;
	b4 = (uint32_t)c->ac4 * (uint32_t)(x3 + 32768) / 32768;
	b7 = ((uint32_t)up - b3) * (50000 >> 3);

	if (b7 < 0x80000000)
		p = (b7 * 2) / b4;
	else
		p = (b7 / b4) * 2;

	x1 = (p / 256) * (p / 256);
	x1 = (x1 * 3038) / 65536;
	x2 = (-7357 * p) / 65536;
	p = p + (x1 + x2 + 3791) / 16;

	return p;
}


int bmp180_get_measurement(void *ctx, float *temp, float *pressure, float *humidity)
{
	bmp180_context_t *c = (bmp180_context_t*)ctx;
	int res;
	uint16_t t_raw;
	uint32_t p;

	if (c->state == 0) {
		/* Read temperature measurement */
		res = i2c_read_register_u16(c->i2c, c->addr, REG_OUT_MSB, &t_raw);
		if (res)
			return -2;
		c->temp = bmp180_compensated_t(t_raw, c);
		DEBUG_PRINT("raw temp = %u, temp=%ld\n", t_raw, c->temp);
		c->state = 1;
	} else {
		/* Read pressure measurement */
		res = i2c_read_register_u24(c->i2c, c->addr, REG_OUT_MSB, &p);
		if (res)
			return -3;
		p >>= 5;
		c->pressure = bmp180_compensated_p(p, c);
		DEBUG_PRINT("raw pressure = %lu, pressure = %ld\n", p, c->pressure);
		c->state = 0;
	}

	*temp = c->temp / 10.0;
	*pressure = (float)c->pressure;
	*humidity = -1.0;

	return 0;
}
