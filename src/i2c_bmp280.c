/* i2c_bmp280.c
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

/* BMP280 Registers */
#define REG_CALIB         0x88 // 0x88 - 0xa1
#define REG_ID            0xd0
#define REG_RESET         0xe0 // write 0xb6 to reset
#define REG_STATUS        0xf3
#define REG_CTRL_MEAS     0xf4
#define REG_CONFIG        0xf5
#define REG_PRESS_MSB     0xf7
#define REG_PRESS_LSB     0xf8
#define REG_PRESS_XLSB    0xf9
#define REG_TEMP_MSB      0xfa
#define REG_TEMP_LSB      0xfb
#define REG_TEMP_XLSB     0xfc


#define BMP280_DEVICE_ID  0x58


typedef struct bmp280_context_t {
	i2c_inst_t *i2c;
	uint8_t addr;
	// Calibration Data
	uint16_t t1;
	int16_t t2;
	int16_t t3;
	uint16_t p1;
	int16_t p2;
	int16_t p3;
	int16_t p4;
	int16_t p5;
	int16_t p6;
	int16_t p7;
	int16_t p8;
	int16_t p9;
} bmp280_context_t;



void* bmp280_init(i2c_inst_t *i2c, uint8_t addr)
{
	bmp280_context_t *ctx = calloc(1, sizeof(bmp280_context_t));
	uint8_t buf[26];
	uint8_t val = 0;
	int res;

	if (!ctx)
		return NULL;
	ctx->i2c = i2c;
	ctx->addr = addr;


	/* Read and verify device ID */
	res  = i2c_read_register_u8(i2c, addr, REG_ID, &val);
	if (res || (val  != BMP280_DEVICE_ID))
		goto panic;

	/* Reset Sensor */
	res = i2c_write_register_u8(i2c, addr, REG_RESET, 0xb6);
	if (res)
		goto panic;

	/* Wait for sensor to soft reset (reset should take 2ms per datasheet)  */
	sleep_ms(10);

	/* Set CTRL_MEAS register: oversampling 16x (p)/ 2x (t), normal mode  */
	res = i2c_write_register_u8(i2c, addr, REG_CTRL_MEAS, 0xab);
	if (res)
		goto panic;

	/* Set CONFIG register: t_standby = 0.5ms, filter = 16   */
	res = i2c_write_register_u8(i2c, addr, REG_CONFIG, 0x1c);
	if (res)
		goto panic;



	/* Read calibration data */
	res = i2c_read_register_block(i2c, addr, REG_CALIB, buf, 26, 0);
	if (res)
		goto panic;

	ctx->t1 = (buf[0] | (buf[1] << 8));
	ctx->t2 = (buf[2] | (buf[3] << 8));
	ctx->t3 = (buf[4] | (buf[5] << 8));
	DEBUG_PRINT("t1=%04x %u\n", ctx->t1, ctx->t1);
	DEBUG_PRINT("t2=%04x %d\n", ctx->t2, ctx->t2);
	DEBUG_PRINT("t2=%04x %d\n", ctx->t3, ctx->t3);

	ctx->p1 = (buf[6] | (buf[7] << 8));
	ctx->p2 = (buf[8] | (buf[9] << 8));
	ctx->p3 = (buf[10] | (buf[11] << 8));
	ctx->p4 = (buf[12] | (buf[13] << 8));
	ctx->p5 = (buf[14] | (buf[15] << 8));
	ctx->p6 = (buf[16] | (buf[17] << 8));
	ctx->p7 = (buf[18] | (buf[19] << 8));
	ctx->p8 = (buf[20] | (buf[21] << 8));
	ctx->p9 = (buf[22] | (buf[23] << 8));
	DEBUG_PRINT("p1=%04x %u\n", ctx->p1, ctx->p1);
	DEBUG_PRINT("p2=%04x %d\n", ctx->p2, ctx->p2);
	DEBUG_PRINT("p3=%04x %d\n", ctx->p3, ctx->p3);
	DEBUG_PRINT("p4=%04x %d\n", ctx->p4, ctx->p4);
	DEBUG_PRINT("p5=%04x %d\n", ctx->p5, ctx->p5);
	DEBUG_PRINT("p6=%04x %d\n", ctx->p6, ctx->p6);
	DEBUG_PRINT("p7=%04x %d\n", ctx->p7, ctx->p7);
	DEBUG_PRINT("p8=%04x %d\n", ctx->p8, ctx->p8);
	DEBUG_PRINT("p9=%04x %d\n", ctx->p9, ctx->p9);

	return ctx;

panic:
	free(ctx);
	return NULL;
}


int bmp280_start_measurement(void *ctx)
{
	/* Nothing to do, sensor is in continuous measurement mode... */

	return 1000;  /* measurement should be available after 1s */
}


/* Temperature compensation based on datasheet example */
static int32_t bmp280_compensate_t(int32_t adc_t, int32_t *t_fine, bmp280_context_t *c)
{
	int32_t var1, var2, t;

	var1 = ((((adc_t >> 3) - ((int32_t)c->t1 << 1))) * ((int32_t)c->t2)) >> 11;
	var2 = (((((adc_t >> 4) - ((int32_t)c->t1)) * ((adc_t >> 4) - ((int32_t)c->t1))) >> 12) * ((int32_t)c->t3)) >> 14;
	*t_fine = var1 + var2;
	t = (*t_fine * 5 + 128) >>  8;

	return t;
}


/*  Pressure compensation based on datasheet example */
static uint32_t bmp280_compensate_p(int32_t adc_p, int32_t t_fine, bmp280_context_t *c)
{
	int64_t var1, var2, p;

	var1 = ((int64_t)t_fine) - 128000;
	var2 = var1 * var1 * (int64_t)c->p6;
	var2 = var2 + ((var1 * (int64_t)c->p5) << 17);
	var2 = var2 + (((int64_t)c->p4) << 35);
	var1 = ((var1 * var1 * (int64_t)c->p3) >> 8) + ((var1 * (int64_t)c->p2) << 12);
	var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)c->p1) >> 33;
	if (var1 == 0)
		return 0;
	p = 1048576 - adc_p;
	p = (((p << 31) - var2) * 3125) / var1;
	var1 = (((int64_t)c->p9) * (p >> 13) * (p >> 13)) >> 25;
	var2 = (((int64_t)c->p8) * p) >> 19;
	p = ((p + var1 + var2) >> 8) + (((int64_t)c->p7) << 4);

	return (uint32_t)p;
}


int bmp280_get_measurement(void *ctx, float *temp, float *pressure, float *humidity)
{
	bmp280_context_t *c = (bmp280_context_t*)ctx;
	int res;
	uint8_t buf[6];
	int32_t p_raw, t_raw, t_fine;


	/* Read Pressure & Temperature registers */
	res = i2c_read_register_block(c->i2c, c->addr, REG_PRESS_MSB, buf, 6, 0);
	if (res)
		return -2;

	p_raw = (buf[0] << 12) | (buf[1] << 4) | (buf[2] >> 4);
	t_raw = (buf[3] << 12) | (buf[4] << 4) | (buf[5] >> 4);
	DEBUG_PRINT("p_raw=%ld, t_raw=%ld\n", p_raw, t_raw);

	*temp = bmp280_compensate_t(t_raw, &t_fine, c) / 100.0;
	*pressure = bmp280_compensate_p(p_raw, t_fine, c) / 256.0;
	*humidity = -1.0;

	return 0;
}
