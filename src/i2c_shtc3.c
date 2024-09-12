/* i2c_shtc3.c
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

/* SHTC3 Commands */
#define CMD_SLEEP              0xb098
#define CMD_WAKEUP             0x3517
#define CMD_MEASURE            0x58e0
#define CMD_RESET              0x805d
#define CMD_ID                 0xefc8

#define SHTC3_DEVICE_ID        0x0807
#define SHTC3_DEVICE_ID_MASK   0x083f


typedef struct shtc3_context_t {
	i2c_inst_t *i2c;
	uint8_t addr;
} shtc3_context_t;


/* CRC-8 Lookup Table (Polymonial 0x31) */
static const uint8_t shtc3_crc_lookup_table[] = {
	0x00,0x31,0x62,0x53,0xC4,0xF5,0xA6,0x97,0xB9,0x88,0xDB,0xEA,0x7D,0x4C,0x1F,0x2E,
	0x43,0x72,0x21,0x10,0x87,0xB6,0xE5,0xD4,0xFA,0xCB,0x98,0xA9,0x3E,0x0F,0x5C,0x6D,
	0x86,0xB7,0xE4,0xD5,0x42,0x73,0x20,0x11,0x3F,0x0E,0x5D,0x6C,0xFB,0xCA,0x99,0xA8,
	0xC5,0xF4,0xA7,0x96,0x01,0x30,0x63,0x52,0x7C,0x4D,0x1E,0x2F,0xB8,0x89,0xDA,0xEB,
	0x3D,0x0C,0x5F,0x6E,0xF9,0xC8,0x9B,0xAA,0x84,0xB5,0xE6,0xD7,0x40,0x71,0x22,0x13,
	0x7E,0x4F,0x1C,0x2D,0xBA,0x8B,0xD8,0xE9,0xC7,0xF6,0xA5,0x94,0x03,0x32,0x61,0x50,
	0xBB,0x8A,0xD9,0xE8,0x7F,0x4E,0x1D,0x2C,0x02,0x33,0x60,0x51,0xC6,0xF7,0xA4,0x95,
	0xF8,0xC9,0x9A,0xAB,0x3C,0x0D,0x5E,0x6F,0x41,0x70,0x23,0x12,0x85,0xB4,0xE7,0xD6,
	0x7A,0x4B,0x18,0x29,0xBE,0x8F,0xDC,0xED,0xC3,0xF2,0xA1,0x90,0x07,0x36,0x65,0x54,
	0x39,0x08,0x5B,0x6A,0xFD,0xCC,0x9F,0xAE,0x80,0xB1,0xE2,0xD3,0x44,0x75,0x26,0x17,
	0xFC,0xCD,0x9E,0xAF,0x38,0x09,0x5A,0x6B,0x45,0x74,0x27,0x16,0x81,0xB0,0xE3,0xD2,
	0xBF,0x8E,0xDD,0xEC,0x7B,0x4A,0x19,0x28,0x06,0x37,0x64,0x55,0xC2,0xF3,0xA0,0x91,
	0x47,0x76,0x25,0x14,0x83,0xB2,0xE1,0xD0,0xFE,0xCF,0x9C,0xAD,0x3A,0x0B,0x58,0x69,
	0x04,0x35,0x66,0x57,0xC0,0xF1,0xA2,0x93,0xBD,0x8C,0xDF,0xEE,0x79,0x48,0x1B,0x2A,
	0xC1,0xF0,0xA3,0x92,0x05,0x34,0x67,0x56,0x78,0x49,0x1A,0x2B,0xBC,0x8D,0xDE,0xEF,
	0x82,0xB3,0xE0,0xD1,0x46,0x77,0x24,0x15,0x3B,0x0A,0x59,0x68,0xFF,0xCE,0x9D,0xAC
};

static uint8_t crc8(uint8_t *buf, size_t len)
{
	uint8_t crc = 0xff;

	for (int i = 0; i < len; i++) {
		crc = shtc3_crc_lookup_table[crc ^ buf[i]];
	}

	return crc;
}


static int shtc3_read_u16(i2c_inst_t *i2c, uint8_t addr, uint16_t *val, bool nostop)
{
	uint8_t buf[3], crc;
	int res;

	DEBUG_PRINT("args=%p,%02x,%p\n", i2c, addr, val);

	res = i2c_read_raw(i2c, addr, buf, 3, nostop);
	if (res) {
		DEBUG_PRINT("read failed (%d)\n", res);
		return -1;
	}

	*val = (buf[0] << 8) | buf[1];
	crc = crc8(buf, 2);
	DEBUG_PRINT("read ok: [%02x %02x (%02x)] %04x (%u), crc=%02x\n", buf[0], buf[1], buf[2], *val, *val, crc);

	if (crc != buf[2]) {
		DEBUG_PRINT("checksum mismatch!\n");
		return -2;
	}

	return 0;
}


void* shtc3_init(i2c_inst_t *i2c, uint8_t addr)
{
	int res;
	uint16_t val = 0;
	shtc3_context_t *ctx = calloc(1, sizeof(shtc3_context_t));

	if (!ctx)
		return NULL;
	ctx->i2c = i2c;
	ctx->addr = addr;


	/* Read and verify device ID */
	res = i2c_write_raw_u16(i2c, addr, CMD_ID, false);
	if (res)
		goto panic;
	sleep_us(10);
	res = shtc3_read_u16(i2c, addr, &val, false);
	if (res)
		goto panic;
	if ((val & SHTC3_DEVICE_ID_MASK) != SHTC3_DEVICE_ID)
		goto panic;


	/* Reset sensor */
	res = i2c_write_raw_u16(i2c, addr, CMD_RESET, false);
	if (res)
		goto panic;
	sleep_us(250);


	/* Read and verify device ID again... */
	res = i2c_write_raw_u16(i2c, addr, CMD_ID, false);
	if (res)
		goto panic;
	sleep_us(10);
	res = shtc3_read_u16(i2c, addr, &val, false);
	if (res)
		goto panic;
	if ((val & SHTC3_DEVICE_ID_MASK) != SHTC3_DEVICE_ID)
		goto panic;

	return ctx;

panic:
	free(ctx);
	return NULL;
}


int shtc3_start_measurement(void *ctx)
{
	shtc3_context_t *c = (shtc3_context_t*)ctx;
	int res;

	/* Initiate measurement */
	res = i2c_write_raw_u16(c->i2c, c->addr, CMD_MEASURE, false);
	if (res)
		return -1;

	return 13;  /* measurement should be available after 12.1ms */
}


int shtc3_get_measurement(void *ctx, float *temp, float *pressure, float *humidity)
{
	shtc3_context_t *c = (shtc3_context_t*)ctx;
	int res;
	uint8_t buf[6];
	uint16_t t_raw = 0;
	uint16_t h_raw = 0;

	/* Get Measurement */
	res = i2c_read_raw(c->i2c, c->addr, buf, 6, false);
	if (res)
		return -1;

	/* Check CRC of received values */
	if (crc8(&buf[0], 2) != buf[2])
		return -2;
	if (crc8(&buf[3], 2) != buf[5])
		return -3;

	h_raw = (buf[0] << 8) | buf[1];
	t_raw = (buf[3] << 8) | buf[4];
	DEBUG_PRINT("h_raw = %u, t_raw = %u\n", h_raw, t_raw);

	*temp = -45 + 175 * (double) t_raw / 65536;
	*pressure = -1.0;
	*humidity = 100 * (double) h_raw / 65536;
	DEBUG_PRINT("temp: %0.1f, humidity: %0.1f\n", *temp, *humidity);

	return 0;
}


