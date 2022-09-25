/* httpd.c
   Copyright (C) 2022 Timo Kokkonen <tjko@iki.fi>

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
#include <time.h>
#include <assert.h>
#include "hardware/rtc.h"
#include "pico/stdlib.h"
#include "cJSON.h"

#include "fanpico.h"




u16_t csv_stats(char *insert, int insertlen, u16_t current_tag_part, u16_t *next_tag_part)
{
	const struct fanpico_state *st = fanpico_state;
	static char buf[512];
	static char *p;
	static u16_t part;
	static size_t buf_left;
	char row[128];
	double rpm;
	int i;
	size_t printed, count;

	if (current_tag_part == 0) {
		/* Generate 'output' into a buffer that then will be fed in chunks to LwIP... */
		buf[0] = 0;

		for (i = 0; i < FAN_COUNT; i++) {
			rpm = st->fan_freq[i] * 60 / cfg->fans[i].rpm_factor;
			snprintf(row, sizeof(row), "fan%d,\"%s\",%.0lf,%.2f,%.1f\n",
				i+1,
				cfg->fans[i].name,
				rpm,
				st->fan_freq[i],
				st->fan_duty[i]);
			strncat(buf, row, sizeof(buf) - 1);
		}
		for (i = 0; i < MBFAN_COUNT; i++) {
			rpm = st->mbfan_freq[i] * 60 / cfg->mbfans[i].rpm_factor;
			snprintf(row, sizeof(row), "mbfan%d,\"%s\",%.0lf,%.2f,%.1f\n",
				i+1,
				cfg->mbfans[i].name,
				rpm,
				st->mbfan_freq[i],
				st->mbfan_duty[i]);
			strncat(buf, row, sizeof(buf) - 1);
		}
		for (i = 0; i < SENSOR_COUNT; i++) {
			snprintf(row, sizeof(row), "sensor%d,\"%s\",%.1lf\n",
				i+1,
				cfg->sensors[i].name,
				st->temp[i]);
			strncat(buf, row, sizeof(buf) - 1);
		}

		p = buf;
		buf_left = strlen(buf);
		part = 1;
	}

	/* Copy a part of the multi-part response into LwIP buffer ...*/
	count = (buf_left < insertlen - 1 ? buf_left : insertlen - 1);
	memcpy(insert, p, count);

	p += count;
	printed = count;
	buf_left -= count;

	if (buf_left > 0)
		*next_tag_part = part++;

	return printed;
}


u16_t json_stats(char *insert, int insertlen, u16_t current_tag_part, u16_t *next_tag_part)
{
	const struct fanpico_state *st = fanpico_state;
	cJSON *json = NULL;
	static char *buf = NULL;
	static char *p;
	static u16_t part;
	static size_t buf_left;
	int i;
	size_t printed, count;

	if (current_tag_part == 0) {
		/* Generate 'output' into a buffer that then will be fed in chunks to LwIP... */
		cJSON *array, *o;

		if (!(json = cJSON_CreateObject()))
			goto panic;

		/* Fans */
		if (!(array = cJSON_CreateArray()))
			goto panic;
		for (i = 0; i < FAN_COUNT; i++) {
			double rpm = st->fan_freq[i] * 60 / cfg->fans[i].rpm_factor;

			if (!(o = cJSON_CreateObject()))
				goto panic;

			cJSON_AddItemToObject(o, "fan", cJSON_CreateNumber(i+1));
			cJSON_AddItemToObject(o, "name", cJSON_CreateString(cfg->fans[i].name));
			cJSON_AddItemToObject(o, "rpm", cJSON_CreateNumber(rpm));
			cJSON_AddItemToObject(o, "frequency", cJSON_CreateNumber(st->fan_freq[i]));
			cJSON_AddItemToObject(o, "duty_cycle", cJSON_CreateNumber(st->fan_duty[i]));
			cJSON_AddItemToArray(array, o);
		}
		cJSON_AddItemToObject(json, "fans", array);

		/* MB Fans */
		if (!(array = cJSON_CreateArray()))
			goto panic;
		for (i = 0; i < MBFAN_COUNT; i++) {
			double rpm = st->mbfan_freq[i] * 60 / cfg->mbfans[i].rpm_factor;

			if (!(o = cJSON_CreateObject()))
				goto panic;

			cJSON_AddItemToObject(o, "mbfan", cJSON_CreateNumber(i+1));
			cJSON_AddItemToObject(o, "name", cJSON_CreateString(cfg->mbfans[i].name));
			cJSON_AddItemToObject(o, "rpm", cJSON_CreateNumber(rpm));
			cJSON_AddItemToObject(o, "frequency", cJSON_CreateNumber(st->mbfan_freq[i]));
			cJSON_AddItemToObject(o, "duty_cycle", cJSON_CreateNumber(st->mbfan_duty[i]));
			cJSON_AddItemToArray(array, o);
		}
		cJSON_AddItemToObject(json, "mbfans", array);

		/* MB Fans */
		if (!(array = cJSON_CreateArray()))
			goto panic;
		for (i = 0; i < SENSOR_COUNT; i++) {
			if (!(o = cJSON_CreateObject()))
				goto panic;

			cJSON_AddItemToObject(o, "sensor", cJSON_CreateNumber(i+1));
			cJSON_AddItemToObject(o, "name", cJSON_CreateString(cfg->sensors[i].name));
			cJSON_AddItemToObject(o, "temperature", cJSON_CreateNumber(st->temp[i]));
			cJSON_AddItemToArray(array, o);
		}
		cJSON_AddItemToObject(json, "mbfans", array);


		if (!(buf = cJSON_Print(json)))
			goto panic;
		cJSON_Delete(json);
		json = NULL;

		p = buf;
		buf_left = strlen(buf);
		part = 1;
	}

	/* Copy a part of the multi-part response into LwIP buffer ...*/
	count = (buf_left < insertlen - 1 ? buf_left : insertlen - 1);
	memcpy(insert, p, count);

	p += count;
	printed = count;
	buf_left -= count;

	if (buf_left > 0) {
		*next_tag_part = part++;
	} else {
		free(buf);
		buf = p = NULL;
	}

	return printed;

panic:
	if (json)
		cJSON_Delete(json);
	return 0;
}


u16_t fanpico_ssi_handler(const char *tag, char *insert, int insertlen,
			u16_t current_tag_part, u16_t *next_tag_part)
{
	const struct fanpico_state *st = fanpico_state;
	size_t printed = 0;

	/* printf("ssi_handler(\"%s\",%lx,%d,%u,%u)\n", tag, (uint32_t)insert, insertlen, current_tag_part, *next_tag_part); */

	if (!strncmp(tag, "datetime", 8)) {
		datetime_t t;
		if (rtc_get_datetime(&t)) {
			printed = snprintf(insert, insertlen, "%04d-%02d-%02d %02d:%02d:%02d",
					t.year, t.month, t.day, t.hour, t.min, t.sec);
		}
	}
	else if (!strncmp(tag, "model", 5)) {
		printed = snprintf(insert, insertlen, "%s", FANPICO_MODEL);
	}
	else if (!strncmp(tag, "version", 5)) {
		printed = snprintf(insert, insertlen, "%s", FANPICO_VERSION);
	}
	else if (!strncmp(tag, "name", 4)) {
		printed = snprintf(insert, insertlen, "%s", cfg->name);
	}
	else if (!strncmp(tag, "fanrow", 6)) {
		uint8_t i = tag[6] - '1';
		if (i < FAN_COUNT) {
			printed = snprintf(insert, insertlen, "<td>%d<td>%s<td>%0.0f<td>%0.0f %%",
					i + 1,
					cfg->fans[i].name,
					st->fan_freq[i],
					st->fan_duty[i]);
		}
	}
	else if (!strncmp(tag, "mfanrow", 7)) {
		uint8_t i = tag[7] - '1';
		if (i < MBFAN_COUNT) {
			printed = snprintf(insert, insertlen, "<td>%d<td>%s<td>%0.0f<td>%0.0f %%",
					i + 1,
					cfg->mbfans[i].name,
					st->mbfan_freq[i],
					st->mbfan_duty[i]);
		}
	}
	else if (!strncmp(tag, "sensrow", 7)) {
		uint8_t i = tag[7] - '1';
		if (i < SENSOR_COUNT) {
			printed = snprintf(insert, insertlen, "<td>%d<td>%s<td>%0.1f C",
					i + 1,
					cfg->sensors[i].name,
					st->temp[i]);
		}
	}
	else if (!strncmp(tag, "csvstat", 7)) {
		printed = csv_stats(insert, insertlen, current_tag_part, next_tag_part);
	}
	else if (!strncmp(tag, "jsonstat", 8)) {
		printed = json_stats(insert, insertlen, current_tag_part, next_tag_part);
	}


	/* Check if snprintf() output was truncated... */
	printed = (printed >= insertlen ? insertlen - 1 : printed);
	/* printf("printed=%u\n", printed); */

	return printed;
}

