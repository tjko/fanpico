/* i2c.h
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

#ifndef FANPICO_I2C_H
#define FANPICO_I2C_H 1

#include "hardware/i2c.h"



#define I2C_DEBUG 0


#if I2C_DEBUG > 0
#define DEBUG_PRINT(fmt, ...)						\
	do {								\
		uint64_t t = to_us_since_boot(get_absolute_time());	\
		printf("[%6llu.%06llu] %s:%d: %s(): " fmt,		\
			(t / 1000000), (t % 1000000),			\
			__FILE__, __LINE__, __func__,			\
			##__VA_ARGS__);					\
	} while (0)
#else
#define DEBUG_PRINT(...)
#endif


extern uint i2c_current_baudrate;  // kHz

#define I2C_TIMEOUT_SCALE_FACTOR (10000 / i2c_current_baudrate)


// timeouts in us (at 1000kHz)
#define I2C_READ_BASE_TIMEOUT 10000
#define I2C_WRITE_BASE_TIMEOUT 10000

#define I2C_READ_TIMEOUT(x) ((I2C_READ_BASE_TIMEOUT + (x * 250)) * 10 / I2C_TIMEOUT_SCALE_FACTOR)
#define I2C_WRITE_TIMEOUT(x) ((I2C_WRITE_BASE_TIMEOUT + (x * 250)) * 10 / I2C_TIMEOUT_SCALE_FACTOR)


typedef void* (i2c_init_func_t)(i2c_inst_t *i2c, uint8_t addr);
typedef int (i2c_start_measurement_func_t)(void *ctx);
typedef int (i2c_get_measurement_func_t)(void *ctx, float *temp, float *pressure, float *humidity);

typedef struct i2c_sensor_entry {
	const char* name;
	i2c_init_func_t *init;
	i2c_start_measurement_func_t *start_measurement;
	i2c_get_measurement_func_t *get_measurement;
} i2c_sensor_entry_t;


/* Helper functions for reading/writing sensor registers */
int i2c_read_register_block(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, uint8_t *buf, size_t len);
int i2c_read_register_u24(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, uint32_t *val);
int i2c_read_register_u16(i2c_inst_t *i2c, uint8_t i2c_addr, uint8_t reg, uint16_t *val);
int i2c_read_register_u8(i2c_inst_t *i2c, uint8_t i2c_addr, uint8_t reg, uint8_t *val);
int i2c_write_register_block(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, const uint8_t *buf, size_t len);
int i2c_write_register_u16(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, uint16_t val);
int i2c_write_register_u8(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, uint8_t val);

int i2c_read_raw(i2c_inst_t *i2c, uint8_t addr, uint8_t *buf, size_t len, bool nostop);
int i2c_write_raw_u16(i2c_inst_t *i2c, uint8_t addr, uint16_t cmd, bool nostop);

int32_t twos_complement(uint32_t value, uint8_t bits);




#endif /* FANPICO_I2C_H */
