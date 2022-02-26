/* sensors.c
   Copyright (C) 2021-2022 Timo Kokkonen <tjko@iki.fi>

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
#include <math.h>
#include <assert.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"

#include "fanpico.h"



float get_pico_temp()
{
	uint16_t raw;
	float temp, volt;
	float cal_value = 0.0;

	adc_select_input(4);
	raw = adc_read();
	volt = raw * (3.0 / (1 << 12));
	temp = 27 - ((volt - 0.706) / 0.001721) + cal_value;

	printf("get_pico_temp(): raw=%u, volt=%f, temp=%f\n", raw, volt, temp);
	return roundf(temp);
}

float get_thermistor_temp(uint8_t input)
{
	uint16_t raw;
	float r, temp, volt;

	adc_select_input(input);
	raw = adc_read();
	volt = raw * (3.0 / (1 << 12));
	r = 10000.0 / ( ((float)(1<<12) / raw ) - 1);
	temp = log(r / 10000.0);
	temp /= 3950.0;
	temp += 1.0 / (25.0 + 273.15);
	temp = 1.0 / temp;
	temp -= 273.15;

	printf("get_thermistor_temp(): raw=%u, volt=%f, r=%f temp=%f\n", raw, volt, r,  temp);
	return roundf(temp);
}

double sensor_get_temp(struct sensor_input *sensor, double temp)
{
	double newval;

	newval = (temp * sensor->temp_coefficient) + sensor->temp_offset;

	return newval;
}


double sensor_get_duty(struct sensor_input *sensor, double temp)
{
	int i;
	double a, t;
	struct temp_map *map;

	t = sensor_get_temp(sensor, temp);
	map = &sensor->map;

	if (t <= map->temp[0][0])
		return map->temp[0][1];

	i = 1;
	while (i < map->points && map->temp[i][0] < t)
		i++;

	if (t >= map->temp[i][0])
		return map->temp[i][1];

	a = (double)(map->temp[i][1] - map->temp[i-1][1]) / (double)(map->temp[i][0] - map->temp[i-1][0]);
	return map->temp[i-1][1] + a * (t - map->temp[i-1][0]);
}




