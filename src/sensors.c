/* sensors.c
   Copyright (C) 2021-2023 Timo Kokkonen <tjko@iki.fi>

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


/* Map of temperature sensor ADC inputs
 */
uint8_t sensor_adc_map[SENSOR_MAX_COUNT] = {
	SENSOR1_READ_ADC,
	SENSOR2_READ_ADC,
	SENSOR3_READ_ADC,
};


double get_temperature(uint8_t input, const struct fanpico_config *config)
{
	uint8_t pin;
	uint32_t raw = 0;
	uint64_t start, end;
	double t, r, volt;
	int i;
	const struct sensor_input *sensor;

	if (input >= SENSOR_COUNT)
		return 0.0;

	start = to_us_since_boot(get_absolute_time());

	sensor = &config->sensors[input];

	pin = sensor_adc_map[input];
	adc_select_input(pin);
	for (i = 0; i < ADC_AVG_WINDOW; i++) {
		raw += adc_read();
	}
	raw /= ADC_AVG_WINDOW;
	volt = raw * (ADC_REF_VOLTAGE / ADC_MAX_VALUE);

	if (sensor->type == TEMP_INTERNAL) {
		t = 27.0 - ((volt - 0.706) / 0.001721);
		t = t * sensor->temp_coefficient + sensor->temp_offset;
	} else {
		if (volt > 0.1 && volt < ADC_REF_VOLTAGE - 0.1) {
			r = SENSOR_SERIES_RESISTANCE / (((double)ADC_MAX_VALUE / raw) - 1);
			t = log(r / sensor->thermistor_nominal);
			t /= sensor->beta_coefficient;
			t += 1.0 / (sensor->temp_nominal + 273.15);
			t = 1.0 / t;
			t -= 273.15;
			t = t * sensor->temp_coefficient + sensor->temp_offset;
		} else {
			t = 0.0;
		}
	}

	/* Apply filter */
	if (sensor->filter != FILTER_NONE) {
		double t_f = filter(sensor->filter, sensor->filter_ctx, t);
		if (t_f != t) {
			log_msg(LOG_DEBUG, "filter sensor%d: %lf -> %lf\n", i+1, t, t_f);
			t = t_f;
		}
	}

	end = to_us_since_boot(get_absolute_time());

	log_msg(LOG_DEBUG, "get_temperature(%d): sensor_type=%u, raw=%u,  volt=%lf, temp=%lf (duration=%llu)",
		input, sensor->type, raw, volt, t, end - start);

	return t;
}


double sensor_get_duty(const struct temp_map *map, double temp)
{
	int i;
	double a, t;

	t = temp;

	if (t <= map->temp[0][0])
		return map->temp[0][1];

	i = 1;
	while (i < map->points-1 && map->temp[i][0] < t)
		i++;

	if (t >= map->temp[i][0])
		return map->temp[i][1];

	a = (double)(map->temp[i][1] - map->temp[i-1][1]) / (double)(map->temp[i][0] - map->temp[i-1][0]);
	return map->temp[i-1][1] + a * (t - map->temp[i-1][0]);
}


double get_vsensor(uint8_t i, struct fanpico_config *config,
		struct fanpico_state *state)
{
	struct vsensor_input *s = &config->vsensors[i];
	double t = state->vtemp[i];

	if (s->mode == VSMODE_MANUAL) {
		/* Copy over values from WRITE:VSENSORx commands ... */
		if (config->vtemp_updated[i] != state->vtemp_updated[i]) {
			t = config->vtemp[i];
			state->vtemp_updated[i] = config->vtemp_updated[i];
		}
		/* Check if should reset temperature back to default due to lack of updates... */
		if (s->timeout > 0 && t != s->default_temp) {
			if (absolute_time_diff_us(state->vtemp_updated[i], get_absolute_time())/1000000
				> s->timeout) {
				log_msg(LOG_INFO,"sensor%d: timeout, temperature reset to default", i + 1);
				t = s->default_temp;
			}
		}
	} else  {
		int count = 0;
		t = 0.0;

		for (int j = 0; j < SENSOR_COUNT && s->sensors[j]; j++) {
			float val = state->temp[s->sensors[j] - 1];
			count++;

			if (s->mode == VSMODE_MAX) {
				if (count == 1 || val > t)
					t = val;
			}
			else if (s->mode == VSMODE_MIN) {
				if (count == 1 || val < t)
					t = val;
			}
			else if (s->mode == VSMODE_AVG) {
				t += val;
			}
			else if (s->mode == VSMODE_DELTA) {
				if (count == 1)
					t = val;
				else if (count == 2)
					t -= val;
			}
		}
		if (s->mode == VSMODE_AVG && count > 0) {
			t /= count;
		}

	}


	/* Apply filter */
	if (s->filter != FILTER_NONE) {
		double t_f = filter(s->filter, s->filter_ctx, t);
		if (t_f != t) {
			log_msg(LOG_DEBUG, "filter vsensor%d: %lf -> %lf\n", i+1, t, t_f);
			t = t_f;
		}
	}

	return t;
}

