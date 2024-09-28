/* i2c_ms8607.c
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

/* MS8607 Registers */

#define RESET_PT           0x1e
#define CONVERT_D1         0x4a // OSR=8192
#define CONVERT_D2         0x5a // OSR=8192
#define READ_ADC           0x00
#define PROM_READ          0xa0

#define RESET_RH           0xfe
#define WRITE_USER         0xe6
#define READ_USER          0xe7
#define MEASURE_RH         0xf5 // No Hold master



typedef struct ms8607_context_t {
	i2c_inst_t *i2c;
	uint8_t addr;
	uint8_t addr2;
	uint8_t state;
	uint16_t prom_pt[7];
	uint16_t prom_rh[7];
	uint32_t d1;
	uint32_t d2;
	float temp;
	float pressure;
	float humidity;
} ms8607_context_t;



static uint8_t crc4_pt(const uint16_t prom[])
{
	uint16_t n_rem = 0;
	uint16_t n_prom[8];

	memcpy(n_prom, prom, 7 * sizeof(uint16_t));
	n_prom[7] = 0;
	n_prom[0] &= 0x0fff;

	for (int cnt = 0; cnt < 16; cnt++) {
		if (cnt % 2 == 1)
			n_rem ^= n_prom[cnt >> 1] & 0xff;
		else
			n_rem ^= n_prom[cnt >> 1] >> 8;

		for (int n_bit = 8; n_bit > 0; n_bit--) {
			if (n_rem & 0x8000)
				n_rem = (n_rem << 1) ^ 0x3000;
			else
				n_rem = (n_rem << 1);
		}
	}

	n_rem = (n_rem >> 12) & 0x000f;
	return n_rem ^ 0x00;
}

#if 0
static uint8_t crc4_rh(const uint16_t prom[])
{
	uint16_t n_rem = 0;
	uint16_t n_prom[8];

 	memcpy(n_prom, prom, 7 * sizeof(uint16_t));
	n_prom[7] = 0;
	n_prom[6] &= 0xfff0;

	for (int cnt = 0; cnt < 16; cnt++) {
		if (cnt % 2 == 1)
			n_rem ^= n_prom[cnt >> 1] & 0xff;
		else
			n_rem ^= n_prom[cnt >> 1] >> 8;

		for (int n_bit = 8; n_bit > 0; n_bit--) {
			if (n_rem & 0x8000)
				n_rem = (n_rem << 1) ^ 0x3000;
			else
				n_rem = (n_rem << 1);
		}
	}

	n_rem = (n_rem >> 12) & 0x000f;
	return n_rem ^ 0x00;
}
#endif

static uint8_t crc_hsensor(uint16_t value)
{
	uint32_t polynom = 0x988000;
	uint32_t msb = 0x800000;
	uint32_t mask = 0xff8000;
	uint32_t result = (uint32_t)value << 8;

	while (msb != 0x80) {
		if (result & msb)
			result = ((result ^ polynom) & mask) | (result & ~mask);
		msb >>= 1;
		mask >>= 1;
		polynom >>= 1;
	}

	return result & 0xff;
}


void* ms8607_init(i2c_inst_t *i2c, uint8_t addr)
{
	ms8607_context_t *ctx = calloc(1, sizeof(ms8607_context_t));
	uint8_t crc, reg;
	int res;

	if (!ctx)
		return NULL;

	ctx->i2c = i2c;
	ctx->addr = 0x76;
	ctx->addr2 = 0x40;
	ctx->state = 0;
	ctx->temp = 0.0;
	ctx->pressure = -1.0;
	ctx->humidity = -1.0;


	/* Reset P&T and RH Sensor */
	res = i2c_write_raw_u8(ctx->i2c, ctx->addr, RESET_PT, false);
	if (res)
		goto panic;
	res = i2c_write_raw_u8(ctx->i2c, ctx->addr2, RESET_RH, false);
	if (res)
		goto panic;
	sleep_ms(15);


	/* Read P&T PROM */
	for (int i = 0; i < 7; i++) {
		res = i2c_read_register_u16(ctx->i2c, ctx->addr, PROM_READ + (i * 2),
					&ctx->prom_pt[i]);
		if (res)
			goto panic;
		sleep_us(100);
	}
	DEBUG_PRINT("C1=%u\n", ctx->prom_pt[1]);
	DEBUG_PRINT("C2=%u\n", ctx->prom_pt[2]);
	DEBUG_PRINT("C3=%u\n", ctx->prom_pt[3]);
	DEBUG_PRINT("C4=%u\n", ctx->prom_pt[4]);
	DEBUG_PRINT("C5=%u\n", ctx->prom_pt[5]);
	DEBUG_PRINT("C6=%u\n", ctx->prom_pt[6]);
	crc = crc4_pt(ctx->prom_pt);
	DEBUG_PRINT("CRC-4: %02x (%02x)\n", crc, ctx->prom_pt[0] >> 12);
	if (crc != (ctx->prom_pt[0] >> 12))
		goto panic;

#if 0
	/* Read RH PROM */
	sleep_us(1000);
	for (int i = 0; i < 7; i++) {
		res = i2c_read_register_u16(ctx->i2c, ctx->addr2, PROM_READ + (i * 2),
					&ctx->prom_rh[i]);
		if (res)
			goto panic;
		sleep_us(1000);
	}
	crc = crc4_rh(ctx->prom_rh);
	DEBUG_PRINT("CRC-4: %02x (%02x)\n", crc, ctx->prom_rh[6] & 0x0f);
	if (crc != (ctx->prom_rh[6] & 0x0f))
		goto panic;
#endif

	/* Read User Register */
	res = i2c_read_register_u8(ctx->i2c, ctx->addr2, READ_USER, &reg);
	if (res)
		goto panic;
	DEBUG_PRINT("Current User Register value: %02x\n", reg);

	/* Update User Register if needed */
	if (reg != (reg & 0x7a)) {
		reg = (reg &0x7a);
		res = i2c_write_register_u8(ctx->i2c, ctx->addr2, WRITE_USER, reg);
		if (res)
			goto panic;
		DEBUG_PRINT("User register changed to: %02x\n", reg);
	}

	return ctx;

panic:
	free(ctx);
	return NULL;
}


int ms8607_start_measurement(void *ctx)
{
	ms8607_context_t *c = (ms8607_context_t*)ctx;
	int res;
	uint8_t cmd = 0;
	uint8_t addr = c->addr;
	int delay = 18;

	switch (c->state) {

	case 0:
		/* Start D1 (pressure) Measurement */
		cmd = CONVERT_D1;
		break;

	case 1:
		/* Start D2 (temperature) Measurement */
		cmd = CONVERT_D2;
		break;

	case 2:
		/* Start Humidity Measurement */
		cmd = MEASURE_RH;
		addr = c->addr2;
		delay = 16;
		break;

	}

	if (cmd) {
		res = i2c_write_raw_u8(c->i2c, addr, cmd, false);
		if (res)
			return -1;
	}

	return delay;
}


int ms8607_get_measurement(void *ctx, float *temp, float *pressure, float *humidity)
{
	ms8607_context_t *c = (ms8607_context_t*)ctx;
	int res;
	uint32_t val;
	uint8_t buf[3], crc;
	int32_t dt, t, p, rh, t2;
	int64_t off, sens, off2, sens2;


	if (c->state == 0 || c->state == 1) {
		res = i2c_read_register_u24(c->i2c, c->addr, READ_ADC, &val);
		if (res)
			return -1;

		if (c->state == 0) {
			c->d1 = val;
		} else {
			c->d2 = val;

			DEBUG_PRINT("D1 = %lu\n", c->d1);
			DEBUG_PRINT("D2 = %lu\n", c->d2);

			dt = (int32_t)c->d2 - ((int32_t)c->prom_pt[5] << 8);
			DEBUG_PRINT("dT = %ld\n", dt);
			t = 2000 + (((int64_t)dt * (int64_t)(c->prom_pt[6])) >> 23);
			DEBUG_PRINT("TEMP = %ld\n", t);

			/* Second order compensation */
			if (t < 2000) {
				t2 = (3 * (int64_t)dt * (int64_t)dt) >> 33;
				off2 = (61 * ((int64_t)t - 2000) * ((int64_t)t - 2000)) >> 4;
				sens2 = (29 * ((int64_t)t - 2000) * ((int64_t)t - 2000)) >> 4;
				if (t < -1500) {
					off2 += 17 * ((int64_t)t + 1500) * ((int64_t)t + 1500);
					sens2 += 9 * ((int64_t)t + 1500) * ((int64_t)t + 1500);
				}
			} else {
				t2 = (5 * ((int64_t)dt * (int64_t)dt)) >> 38;
				off2 = 0;
				sens2 = 0;
			}

			off = ((int64_t)c->prom_pt[2] << 17) + (((int64_t)(c->prom_pt[4] * dt)) >> 6);
			off -= off2;
			DEBUG_PRINT("OFF = %lld\n", off);
			sens = ((int64_t)c->prom_pt[1] << 16) + (((int64_t)c->prom_pt[3] * dt) >>  7);
			sens -= sens2;
			DEBUG_PRINT("SENS = %lld\n", sens);
			p = (((c->d1 * sens) >> 21) - off) >> 15;
			DEBUG_PRINT("P = %ld\n", p);

			c->temp = (t - t2) / 100.0;
			c->pressure = p / 100.0;
		}
	}
	else {
		res = i2c_read_raw(c->i2c, c->addr2, buf, sizeof(buf), false);
		if (res)
			return -2;
		val = ((buf[0] << 8) | buf[1]); // & 0xfffc;
		crc = crc_hsensor(val);
		if (crc != buf[2]) {
			DEBUG_PRINT("CRC mismatch: %02x (%02x)\n", crc, buf[2]);
			return -2;
		}

		rh = -600 + ((12500 * val) >> 16);

		c->humidity = rh / 100.0;
	}
	DEBUG_PRINT("state=%d, val=%lu\n", c->state, val);

	c->state++;
	if (c->state > 2)
		c->state = 0;

	*temp = c->temp;
	*pressure = c->pressure;
	*humidity = c->humidity;

	DEBUG_PRINT("temp = %0.1f C, pressure = %0.1f hPa, humidity = %0.1f %%\n",
		*temp, *pressure, *humidity);

	return 0;
}


