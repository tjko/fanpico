/* i2c_adt7410.c
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

/* ADT7410 Registers */
#define REG_TEMP_MSB       0x00
#define REG_TEMP_LSB       0x01
#define REG_STATUS         0x02
#define REG_CONFIG         0x03
#define REG_ID             0x0b
#define REG_RESET          0x2f

#define ADT7410_DEVICE_ID 0xc8  // bits 7-3 (bits 2-0 contain silicon revision)


typedef struct adt7410_context_t {
	i2c_inst_t *i2c;
	uint8_t addr;
} adt7410_context_t;



void* adt7410_init(i2c_inst_t *i2c, uint8_t addr)
{
	int res;
	uint8_t val = 0;
	uint8_t buf[3];
	adt7410_context_t *ctx = calloc(1, sizeof(adt7410_context_t));


	if (!ctx)
		return NULL;
	ctx->i2c = i2c;
	ctx->addr = addr;

	/* Read and verify device ID */
	res  = i2c_read_register_u8(i2c, addr, REG_ID, &val);
	if (res || (val & 0xf8) != ADT7410_DEVICE_ID)
		goto panic;

	/* Reset Sensor */
	buf[0] = REG_RESET;
	res = i2c_write_timeout_us(i2c, addr, buf, 1, false,
				I2C_WRITE_TIMEOUT(1));
	if (res < 1)
		goto panic;

	/* Wait for sensor to soft reset (reset should take 200us per datasheet)  */
	sleep_us(250);

	/* Write configuration register */
	res = i2c_write_register_u8(i2c, addr, REG_CONFIG, 0x80);
	if (res)
		goto panic;

	/* Read configuration register */
	res = i2c_read_register_u8(i2c, addr, REG_CONFIG, &val);
	if (res)
		goto panic;

	/* Check that confuration is now as expected... */
	if (val != 0x80)
		goto panic;

	return ctx;

panic:
	free(ctx);
	return NULL;
}


int adt7410_start_measurement(void *ctx)
{
	/* Nothing to do, sensor is in continuous measurement mode... */

	return 1000;  /* measurement should be available after 1s */
}


int adt7410_get_measurement(void *ctx, float *temp, float *pressure, float *humidity)
{
	adt7410_context_t *c = (adt7410_context_t*)ctx;
	int res;
	uint8_t val;
	uint16_t meas;


	/* Read status register */
	res = i2c_read_register_u8(c->i2c, c->addr, REG_STATUS, &val);
	if (res)
		return -1;

	/* Check !RDY bit */
	if ((val & 0x80) != 0)
		return 1;

	/* Get Measurement */
	res = i2c_read_register_u16(c->i2c, c->addr, REG_TEMP_MSB, &meas);
	if (res)
		return -2;

	*temp = ((int16_t)meas) / 128.0;
	*pressure = -1.0;
	*humidity = -1.0;

	return 0;
}
