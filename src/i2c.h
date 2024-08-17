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

typedef void* (i2c_init_func_t)(i2c_inst_t *i2c, uint8_t addr);
typedef int (i2c_start_measurement_func_t)(void *ctx);
typedef int (i2c_get_measurement_func_t)(void *ctx, float *temp);

typedef struct i2c_sensor_entry {
	const char* name;
	i2c_init_func_t *init;
	i2c_start_measurement_func_t *start_measurement;
	i2c_get_measurement_func_t *get_measurement;
} i2c_sensor_entry_t;

/* i2c_tmp117.c */
void* tmp117_init(i2c_inst_t *i2c, uint8_t addr);
int tmp117_start_measurement(void *ctx);
int tmp117_get_measurement(void *ctx, float *temp);


#endif /* FANPICO_I2C_H */
