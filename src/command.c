/* command.c
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
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <wctype.h>
#include <assert.h>
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "pico/bootrom.h"
#include "pico/util/datetime.h"
#include "hardware/watchdog.h"
#include "hardware/rtc.h"
#include "cJSON.h"
#include "lfs.h"
#include "fanpico.h"


struct cmd_t {
	const char   *cmd;
	uint8_t       min_match;
	struct cmd_t *subcmds;
	int (*func)(const char *cmd, const char *args, int query, char *prev_cmd);
};

struct fanpico_state *st = NULL;
struct fanpico_config *conf = NULL;

/* credits.s */
extern const char fanpico_credits_text[];


int valid_wifi_country(const char *country)
{
	if (!country)
		return 0;

	if (strlen(country) < 2)
		return 0;

	if (!(country[0] >= 'A' && country[0] <= 'Z'))
		return 0;
	if (!(country[1] >= 'A' && country[1] <= 'Z'))
		return 0;

	if (strlen(country) == 2)
		return 1;

	if (country[2] >= '1' && country[2] <= '9')
		return 1;

	return 0;
}

int cmd_idn(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int i;
	pico_unique_board_id_t board_id;

	if (!query)
		return 1;

	printf("TJKO Industries,FANPICO-%s,", FANPICO_MODEL);
	pico_get_unique_board_id(&board_id);
	for (i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++)
		printf("%02x", board_id.id[i]);
	printf(",%s\n", FANPICO_VERSION);

	return 0;
}

int cmd_usb_boot(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (!query)
		reset_usb_boot(0, 0);

	return 1;
}

int cmd_version(const char *cmd, const char *args, int query, char *prev_cmd)
{
	const char* credits = fanpico_credits_text;

	if (cmd && !query)
		return 1;

	printf("FanPico v%s ", FANPICO_VERSION);
	printf("(Build date: %s)\n\n", __DATE__);

	if (query)
		printf("%s\n", credits);

	return 0;
}

int cmd_fans(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (!query)
		return 1;

	printf("%d\n", FAN_COUNT);
	return 0;
}

int cmd_led(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int mode;

	if (query) {
		printf("%d\n", cfg->led_mode);
	} else {
		mode = atoi(args);
		if (mode >= 0 && mode <= 2) {
			debug(1, "Set system LED mode: %d -> %d\n", cfg->led_mode, mode);
			cfg->led_mode = mode;
		}
	}
	return 0;
}

int cmd_mbfans(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (!query)
		return 1;

	printf("%d\n", MBFAN_COUNT);
	return 0;
}

int cmd_sensors(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (!query)
		return 1;

	printf("%d\n", SENSOR_COUNT);
	return 0;
}

int cmd_null(const char *cmd, const char *args, int query, char *prev_cmd)
{
	debug(1, "null command: %s %s (query=%d)\n", cmd, args, query);
	return 0;
}

int cmd_debug(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int level = atoi(args);

	if (query) {
		printf("%d\n", get_debug_level());
	} else {
		set_debug_level((level < 0 ? 0 : level));
	}
	return 0;
}

int cmd_echo(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int val = atoi(args);

	if (query) {
		printf("%u\n", cfg->local_echo);
	} else {
		cfg->local_echo = (val > 0 ? true : false);
	}
	return 0;
}


int cmd_display_type(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query) {
		printf("%s\n", cfg->display_type);
	} else {
		strncopy(cfg->display_type, args, sizeof(cfg->display_type));
	}
	return 0;
}

int cmd_reset(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (!query) {
		debug(1, "Initiating reboot...\n");
		watchdog_enable(10, 0);
		while (1);
	}
	return 0;
}

int cmd_save_config(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (!query)
		save_config();
	return 0;
}

int cmd_print_config(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query)
		print_config();
	return 0;
}

int cmd_delete_config(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (!query)
		delete_config();
	return 0;
}

int cmd_one(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query)
		printf("1\n");
	return 0;
}

int cmd_zero(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query)
		printf("0\n");
	return 0;
}

int cmd_read(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int i;
	double rpm;

	if (!query)
		return 0;

	for (i = 0; i < MBFAN_COUNT; i++) {
		rpm = st->mbfan_freq[i] * 60 / conf->mbfans[i].rpm_factor;
		printf("mbfan%d,\"%s\",%.0lf,%.2f,%.1f\n", i+1,
			conf->mbfans[i].name,
			rpm,
			st->mbfan_freq[i],
			st->mbfan_duty[i]);
	}

	for (i = 0; i < FAN_COUNT; i++) {
		rpm = st->fan_freq[i] * 60 / conf->fans[i].rpm_factor;
		printf("fan%d,\"%s\",%.0lf,%.2f,%.1f\n", i+1,
			conf->fans[i].name,
			rpm,
			st->fan_freq[i],
			st->fan_duty[i]);
	}

	for (i = 0; i < SENSOR_COUNT; i++) {
		printf("sensor%d,\"%s\",%.1lf\n", i+1,
			conf->sensors[i].name,
			st->temp[i]);
	}

	return 0;
}

int cmd_fan_name(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;

	fan = atoi(&prev_cmd[3]) - 1;
	if (fan >= 0 && fan < FAN_COUNT) {
		if (query) {
			printf("%s\n", conf->fans[fan].name);
		} else {
			debug(1, "fan%d: change name '%s' --> '%s'\n", fan + 1,
				conf->fans[fan].name, args);
			strncopy(conf->fans[fan].name, args, sizeof(conf->fans[fan].name));
		}
	}
	return 0;
}

int cmd_fan_min_pwm(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan, val;

	fan = atoi(&prev_cmd[3]) - 1;
	if (fan >= 0 && fan < FAN_COUNT) {
		if (query) {
			printf("%d\n", conf->fans[fan].min_pwm);
		} else {
			val = atoi(args);
			if (val >= 0 && val <= 100) {
				debug(1, "fan%d: change min PWM %d%% --> %d%%\n", fan + 1,
					conf->fans[fan].min_pwm, val);
				conf->fans[fan].min_pwm = val;
			} else {
				debug(1, "fan%d: invalid new value for min PWM: %d", fan + 1,
					val);
			}
		}
	}
	return 0;
}

int cmd_fan_max_pwm(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan, val;

	fan = atoi(&prev_cmd[3]) - 1;
	if (fan >= 0 && fan < FAN_COUNT) {
		if (query) {
			printf("%d\n", conf->fans[fan].max_pwm);
		} else {
			val = atoi(args);
			if (val >= 0 && val <= 100) {
				debug(1, "fan%d: change max PWM %d%% --> %d%%\n", fan + 1,
					conf->fans[fan].max_pwm, val);
				conf->fans[fan].max_pwm = val;
			} else {
				debug(1, "fan%d: invalid new value for max PWM: %d", fan + 1,
					val);
			}
		}
	}
	return 0;
}

int cmd_fan_pwm_coef(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	float val;

	fan = atoi(&prev_cmd[3]) - 1;
	if (fan >= 0 && fan < FAN_COUNT) {
		if (query) {
			printf("%f\n", conf->fans[fan].pwm_coefficient);
		} else {
			val = atof(args);
			if (val >= 0.0) {
				debug(1, "fan%d: change PWM coefficient %f --> %f\n",
					fan + 1, conf->fans[fan].pwm_coefficient, val);
				conf->fans[fan].pwm_coefficient = val;
			} else {
				debug(1, "fan%d: invalid new value for PWM coefficient: %f",
					fan + 1, val);
			}
		}
	}
	return 0;
}

int cmd_fan_pwm_map(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan, i, count;
	int val;
	char *arg, *t, *saveptr;
	struct pwm_map *map;
	struct pwm_map new_map;

	fan = atoi(&prev_cmd[3]) - 1;
	if (fan < 0 || fan >= FAN_COUNT)
		return 0;
	map = &conf->fans[fan].map;
	new_map.points = 0;

	if (query) {
		for (i = 0; i < map->points; i++) {
			if (i > 0)
				printf(",");
			printf("%u,%u", map->pwm[i][0], map->pwm[i][1]);
		}
		printf("\n");
	} else {
		arg = strdup(args);
		count = 0;
		t = strtok_r(arg, ",", &saveptr);
		while (t) {
			val = atoi(t);
			new_map.pwm[count / 2][count % 2] = val;
			count++;
			t = strtok_r(NULL, ",", &saveptr);
		}
		if ((count >= 4) && (count % 2 == 0)) {
			new_map.points = count / 2;
			*map = new_map;
		} else {
			debug(1,"fan%d: invalid new map: %s\n", fan + 1, args);
		}
		free(arg);
	}

	return 0;
}

int cmd_fan_rpm_factor(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	int val;

	fan = atoi(&prev_cmd[3]) - 1;
	if (fan >= 0 && fan < FAN_COUNT) {
		if (query) {
			printf("%u\n", conf->fans[fan].rpm_factor);
		} else {
			val = atoi(args);
			if (val >= 1 || val <= 8) {
				debug(1, "fan%d: change RPM factor %u --> %d\n",
					fan + 1, conf->fans[fan].rpm_factor, val);
				conf->fans[fan].rpm_factor = val;
			} else {
				debug(1, "fan%d: invalid new value for RPM factor: %d",
					fan + 1, val);
			}
		}
	}
	return 0;
}

int cmd_fan_source(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	int type, val, d_o, d_n;
	char *tok, *saveptr, *param;


	fan = atoi(&prev_cmd[3]) - 1;
	if (fan < 0 || fan >= FAN_COUNT)
		return 0;

	if (query) {
		val = conf->fans[fan].s_id;
		if (conf->fans[fan].s_type != PWM_FIXED)
			val++;
		printf("%s,%u\n",
			pwm_source2str(conf->fans[fan].s_type),
			val);
	} else {
		param = strdup(args);
		if ((tok = strtok_r(param, ",", &saveptr)) != NULL) {
			type = str2pwm_source(tok);
			d_n = (type != PWM_FIXED ? 1 : 0);
			if ((tok = strtok_r(NULL, ",", &saveptr)) != NULL) {
				val = atoi(tok) - d_n;
				if (valid_pwm_source_ref(type, val)) {
					d_o = (conf->fans[fan].s_type != PWM_FIXED ? 1 : 0);
					debug(1, "fan%d: change source %s,%u --> %s,%u\n",
						fan + 1,
						pwm_source2str(conf->fans[fan].s_type),
						conf->fans[fan].s_id + d_o,
						pwm_source2str(type),
						val + d_n);
					conf->fans[fan].s_type = type;
					conf->fans[fan].s_id = val;
				} else {
					debug(1, "fan%d: invalid source: %s",
						fan + 1, args);
				}
			}
		}
		free(param);
	}

	return 0;
}

int cmd_fan_rpm(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	double rpm;

	if (query) {
		fan = atoi(&prev_cmd[3]) - 1;
		if (fan >= 0 && fan < FAN_COUNT) {
			rpm = st->fan_freq[fan] * 60.0 / conf->fans[fan].rpm_factor;
			debug(2,"fan%d (tacho = %fHz) rpm = %.1lf\n", fan + 1,
				st->fan_freq[fan], rpm);
			printf("%.0lf\n", rpm);
		}
	}
	return 0;
}

int cmd_fan_tacho(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	float f;

	if (query) {
		fan = atoi(&prev_cmd[3]) - 1;
		if (fan >= 0 && fan < FAN_COUNT) {
			f = st->fan_freq[fan];
			debug(2,"fan%d tacho = %fHz\n", fan + 1, f);
			printf("%.1f\n", f);
		}
	}
	return 0;
}

int cmd_fan_pwm(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	float d;

	if (query) {
		fan = atoi(&prev_cmd[3]) - 1;
		if (fan >= 0 && fan < FAN_COUNT) {
			d = st->fan_duty[fan];
			debug(2,"fan%d duty = %f%%\n", fan + 1, d);
			printf("%.0f\n", d);
		}
	}
	return 0;
}

int cmd_fan_read(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	double rpm;
	float d, f;

	if (!query)
		return 0;

	if (!strncasecmp(prev_cmd, "fan", 3)) {
		fan = atoi(&prev_cmd[3]) - 1;
	} else {
		fan = atoi(&cmd[3]) - 1;
	}

	if (fan >= 0 && fan < FAN_COUNT) {
		d = st->fan_duty[fan];
		f = st->fan_freq[fan];
		rpm = f * 60.0 / conf->fans[fan].rpm_factor;
		debug(2,"fan%d duty = %f%%, freq = %fHz, speed = %fRPM\n",
			fan + 1, d, f, rpm);
		printf("%.0f,%.1f,%.0f\n", d, f, rpm);
	}

	return 0;
}

int cmd_mbfan_name(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int mbfan;

	mbfan = atoi(&prev_cmd[5]) - 1;
	if (mbfan >= 0 && mbfan < MBFAN_COUNT) {
		if (query) {
			printf("%s\n", conf->mbfans[mbfan].name);
		} else {
			debug(1, "mbfan%d: change name '%s' --> '%s'\n", mbfan + 1,
				conf->mbfans[mbfan].name, args);
			strncopy(conf->mbfans[mbfan].name, args, sizeof(conf->mbfans[mbfan].name));
		}
	}
	return 0;
}

int cmd_mbfan_min_rpm(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan, val;

	fan = atoi(&prev_cmd[5]) - 1;
	if (fan >= 0 && fan < MBFAN_COUNT) {
		if (query) {
			printf("%d\n", conf->mbfans[fan].min_rpm);
		} else {
			val = atoi(args);
			if (val >= 0 && val <= 50000) {
				debug(1, "mbfan%d: change min RPM %d --> %d\n", fan + 1,
					conf->mbfans[fan].min_rpm, val);
				conf->mbfans[fan].min_rpm = val;
			} else {
				debug(1, "mbfan%d: invalid new value for min RPM: %d", fan + 1,
					val);
			}
		}
	}
	return 0;
}

int cmd_mbfan_max_rpm(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan, val;

	fan = atoi(&prev_cmd[5]) - 1;
	if (fan >= 0 && fan < MBFAN_COUNT) {
		if (query) {
			printf("%d\n", conf->mbfans[fan].max_rpm);
		} else {
			val = atoi(args);
			if (val >= 0 && val <= 50000) {
				debug(1, "mbfan%d: change max RPM %d --> %d\n", fan + 1,
					conf->mbfans[fan].max_rpm, val);
				conf->mbfans[fan].max_rpm = val;
			} else {
				debug(1, "fan%d: invalid new value for max RPM: %d", fan + 1,
					val);
			}
		}
	}
	return 0;
}

int cmd_mbfan_rpm_coef(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	float val;

	fan = atoi(&prev_cmd[5]) - 1;
	if (fan >= 0 && fan < MBFAN_COUNT) {
		if (query) {
			printf("%f\n", conf->mbfans[fan].rpm_coefficient);
		} else {
			val = atof(args);
			if (val > 0.0) {
				debug(1, "mbfan%d: change RPM coefficient %f --> %f\n",
					fan + 1, conf->mbfans[fan].rpm_coefficient, val);
				conf->mbfans[fan].rpm_coefficient = val;
			} else {
				debug(1, "mbfan%d: invalid new value for RPM coefficient: %f",
					fan + 1, val);
			}
		}
	}
	return 0;
}

int cmd_mbfan_rpm_factor(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	int val;

	fan = atoi(&prev_cmd[5]) - 1;
	if (fan >= 0 && fan < MBFAN_COUNT) {
		if (query) {
			printf("%u\n", conf->mbfans[fan].rpm_factor);
		} else {
			val = atoi(args);
			if (val >= 1 || val <= 8) {
				debug(1, "mbfan%d: change RPM factor %u --> %d\n",
					fan + 1, conf->mbfans[fan].rpm_factor, val);
				conf->mbfans[fan].rpm_factor = val;
			} else {
				debug(1, "mbfan%d: invalid new value for RPM factor: %d",
					fan + 1, val);
			}
		}
	}
	return 0;
}

int cmd_mbfan_rpm_map(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan, i, count;
	int val;
	char *arg, *t, *saveptr;
	struct tacho_map *map;
	struct tacho_map new_map;

	fan = atoi(&prev_cmd[5]) - 1;
	if (fan < 0 || fan >= MBFAN_COUNT)
		return 0;
	map = &conf->mbfans[fan].map;
	new_map.points = 0;

	if (query) {
		for (i = 0; i < map->points; i++) {
			if (i > 0)
				printf(",");
			printf("%u,%u", map->tacho[i][0], map->tacho[i][1]);
		}
		printf("\n");
	} else {
		arg = strdup(args);
		count = 0;
		t = strtok_r(arg, ",", &saveptr);
		while (t) {
			val = atoi(t);
			new_map.tacho[count / 2][count % 2] = val;
			count++;
			t = strtok_r(NULL, ",", &saveptr);
		}
		if ((count >= 4) && (count % 2 == 0)) {
			new_map.points = count / 2;
			*map = new_map;
		} else {
			debug(1,"mbfan%d: invalid new map: %s\n", fan + 1, args);
		}
		free(arg);
	}

	return 0;
}

int cmd_mbfan_source(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	int type, val, d_o, d_n;
	char *tok, *saveptr, *param;


	fan = atoi(&prev_cmd[5]) - 1;
	if (fan < 0 || fan >= MBFAN_COUNT)
		return 0;

	if (query) {
		val = conf->mbfans[fan].s_id;
		if (conf->mbfans[fan].s_type != TACHO_FIXED)
			val++;
		printf("%s,%u\n",
			tacho_source2str(conf->mbfans[fan].s_type),
			val);
	} else {
		param = strdup(args);
		if ((tok = strtok_r(param, ",", &saveptr)) != NULL) {
			type = str2tacho_source(tok);
			d_n = (type != TACHO_FIXED ? 1 : 0);
			if ((tok = strtok_r(NULL, ",", &saveptr)) != NULL) {
				val = atoi(tok) - d_n;
				if (valid_tacho_source_ref(type, val)) {
					d_o = (conf->mbfans[fan].s_type != TACHO_FIXED ? 1 : 0);
					debug(1, "mbfan%d: change source %s,%u --> %s,%u\n",
						fan + 1,
						tacho_source2str(conf->mbfans[fan].s_type),
						conf->mbfans[fan].s_id + d_o,
						tacho_source2str(type),
						val + d_n);
					conf->mbfans[fan].s_type = type;
					conf->mbfans[fan].s_id = val;
				} else {
					debug(1, "mbfan%d: invalid source: %s",
						fan + 1, args);
				}
			}
		}
		free(param);
	}

	return 0;
}

int cmd_mbfan_rpm(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	double rpm;

	if (query) {
		fan = atoi(&prev_cmd[5]) - 1;
		if (fan >= 0 && fan < MBFAN_COUNT) {
			rpm = st->mbfan_freq[fan] * 60.0 / conf->mbfans[fan].rpm_factor;
			debug(2,"mbfan%d (tacho = %fHz) rpm = %.1lf\n", fan+1,
				st->mbfan_freq[fan], rpm);
			printf("%.0lf\n", rpm);
		}
	}
	return 0;
}

int cmd_mbfan_tacho(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	float f;

	if (query) {
		fan = atoi(&prev_cmd[5]) - 1;
		if (fan >= 0 && fan < MBFAN_COUNT) {
			f = st->mbfan_freq[fan];
			debug(2,"mbfan%d tacho = %fHz\n", fan + 1, f);
			printf("%.1f\n", f);
		}
	}
	return 0;
}

int cmd_mbfan_pwm(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	float d;

	if (query) {
		fan = atoi(&prev_cmd[5]) - 1;
		if (fan >= 0 && fan < MBFAN_COUNT) {
			d = st->mbfan_duty[fan];
			debug(2,"mbfan%d duty = %f%%\n", fan + 1, d);
			printf("%.0f\n", d);
		}
	}
	return 0;
}

int cmd_mbfan_read(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	double rpm;
	float d, f;

	if (!query)
		return 0;

	if (!strncasecmp(prev_cmd, "mbfan", 5)) {
		fan = atoi(&prev_cmd[5]) - 1;
	} else {
		fan = atoi(&cmd[5]) - 1;
	}

	if (fan >= 0 && fan < MBFAN_COUNT) {
		d = st->mbfan_duty[fan];
		f = st->mbfan_freq[fan];
		rpm = f * 60.0 / conf->mbfans[fan].rpm_factor;
		debug(2,"mbfan%d duty = %f%%, freq = %fHz, speed = %fRPM\n",
			fan + 1, d, f, rpm);
		printf("%.0f,%.1f,%.0f\n", d, f, rpm);
	}

	return 0;
}

int cmd_sensor_name(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor;

	sensor = atoi(&prev_cmd[6]) - 1;
	if (sensor >= 0 && sensor < SENSOR_COUNT) {
		if (query) {
			printf("%s\n", conf->sensors[sensor].name);
		} else {
			debug(1, "sensor%d: change name '%s' --> '%s'\n", sensor + 1,
				conf->sensors[sensor].name, args);
			strncopy(conf->sensors[sensor].name, args,
				sizeof(conf->sensors[sensor].name));
		}
	}
	return 0;
}

int cmd_sensor_temp_offset(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor;
	float val;

	sensor = atoi(&prev_cmd[6]) - 1;
	if (sensor >= 0 && sensor < SENSOR_COUNT) {
		if (query) {
			printf("%f\n", conf->sensors[sensor].temp_offset);
		} else {
			val = atof(args);
			debug(1, "sensor%d: change temp offset %f --> %f\n", sensor + 1,
				conf->sensors[sensor].temp_offset, val);
			conf->sensors[sensor].temp_offset = val;
		}
	}
	return 0;
}

int cmd_sensor_temp_coef(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor;
	float val;

	sensor = atoi(&prev_cmd[6]) - 1;
	if (sensor >= 0 && sensor < SENSOR_COUNT) {
		if (query) {
			printf("%f\n", conf->sensors[sensor].temp_coefficient);
		} else {
			val = atof(args);
			if (val > 0.0) {
				debug(1, "sensor%d: change temp coefficient %f --> %f\n",
					sensor + 1, conf->sensors[sensor].temp_coefficient,
					val);
				conf->sensors[sensor].temp_coefficient = val;
			} else {
				debug(1, "sensor%d: invalid temp coefficient: %f\n",
					sensor + 1, val);
			}
		}
	}
	return 0;
}

int cmd_sensor_temp_nominal(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor;
	float val;

	sensor = atoi(&prev_cmd[6]) - 1;
	if (sensor >= 0 && sensor < SENSOR_COUNT) {
		if (query) {
			printf("%.1f\n", conf->sensors[sensor].temp_nominal);
		} else {
			val = atof(args);
			if (val >= -50.0 && val <= 100.0) {
				debug(1, "sensor%d: change temp nominal %.1fC --> %.1fC\n",
					sensor + 1, conf->sensors[sensor].temp_nominal,
					val);
				conf->sensors[sensor].temp_nominal = val;
			} else {
				debug(1, "sensor%d: invalid temp nominal: %f\n",
					sensor + 1, val);
			}
		}
	}
	return 0;
}

int cmd_sensor_ther_nominal(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor;
	float val;

	sensor = atoi(&prev_cmd[6]) - 1;
	if (sensor >= 0 && sensor < SENSOR_COUNT) {
		if (query) {
			printf("%.0f\n", conf->sensors[sensor].thermistor_nominal);
		} else {
			val = atof(args);
			if (val > 0.0) {
				debug(1, "sensor%d: change thermistor nominal %.0f ohm --> %.0f ohm\n",
					sensor + 1, conf->sensors[sensor].thermistor_nominal,
					val);
				conf->sensors[sensor].thermistor_nominal = val;
			} else {
				debug(1, "sensor%d: invalid thermistor nominal: %f\n",
					sensor + 1, val);
			}
		}
	}
	return 0;
}

int cmd_sensor_beta_coef(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor;
	float val;

	sensor = atoi(&prev_cmd[6]) - 1;
	if (sensor >= 0 && sensor < SENSOR_COUNT) {
		if (query) {
			printf("%.0f\n", conf->sensors[sensor].beta_coefficient);
		} else {
			val = atof(args);
			if (val > 0.0) {
				debug(1, "sensor%d: change thermistor beta coefficient %.0f --> %.0f\n",
					sensor + 1, conf->sensors[sensor].beta_coefficient,
					val);
				conf->sensors[sensor].beta_coefficient = val;
			} else {
				debug(1, "sensor%d: invalid thermistor beta coefficient: %f\n",
					sensor + 1, val);
			}
		}
	}
	return 0;
}

int cmd_sensor_temp_map(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor, i, count;
	float val;
	char *arg, *t, *saveptr;
	struct temp_map *map;
	struct temp_map new_map;

	sensor = atoi(&prev_cmd[6]) - 1;
	if (sensor < 0 || sensor >= SENSOR_COUNT)
		return 0;
	map = &conf->sensors[sensor].map;
	new_map.points = 0;

	if (query) {
		for (i = 0; i < map->points; i++) {
			if (i > 0)
				printf(",");
			printf("%f,%f", map->temp[i][0], map->temp[i][1]);
		}
		printf("\n");
	} else {
		arg = strdup(args);
		count = 0;
		t = strtok_r(arg, ",", &saveptr);
		while (t) {
			val = atof(t);
			new_map.temp[count / 2][count % 2] = val;
			count++;
			t = strtok_r(NULL, ",", &saveptr);
		}
		if ((count >= 4) && (count % 2 == 0)) {
			new_map.points = count / 2;
			*map = new_map;
		} else {
			debug(1,"sensor%d: invalid new map: %s\n", sensor + 1, args);
		}
		free(arg);
	}

	return 0;
}

int cmd_sensor_temp(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor;
	float d;

	if (!query)
		return 0;

	if (!strncasecmp(prev_cmd, "sensor", 6)) {
		sensor = atoi(&prev_cmd[6]) - 1;
	} else {
		sensor = atoi(&cmd[6]) - 1;
	}

	if (sensor >= 0 && sensor < SENSOR_COUNT) {
		d = st->temp[sensor];
		debug(2,"sensor%d temperature = %fC\n", sensor + 1, d);
		printf("%.0f\n", d);
	}

	return 0;
}

int cmd_wifi(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query) {
#ifdef WIFI_SUPPORT
		printf("1\n");
#else
		printf("0\n");
#endif
	}
	return 0;
}

int cmd_wifi_ip(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query) {
		network_ip();
	}
	return 0;
}

int cmd_wifi_mac(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query) {
		network_mac();
	}
	return 0;
}

int cmd_wifi_ssid(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query) {
		printf("%s\n", conf->wifi_ssid);
	} else {
		debug(1, "Wi-Fi SSID change '%s' --> '%s'\n",
			conf->wifi_ssid, args);
		strncopy(conf->wifi_ssid, args, sizeof(conf->wifi_ssid));
	}
	return 0;
}

int cmd_wifi_status(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query) {
		network_status();
	}
	return 0;
}

int cmd_wifi_country(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query) {
		printf("%s\n", conf->wifi_country);
	} else {
		if (valid_wifi_country(args)) {
			debug(1, "Wi-Fi Country change '%s' --> '%s'\n",
				conf->wifi_country, args);
			strncopy(conf->wifi_country, args, sizeof(conf->wifi_country));
		} else {
			debug(1, "Invalid Wi-Fi country: %s\n", args);
		}
	}
	return 0;
}

int cmd_wifi_password(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query) {
		printf("%s\n", conf->wifi_passwd);
	} else {
		debug(1, "Wi-Fi Password change '%s' --> '%s'\n",
			conf->wifi_passwd, args);
		strncopy(conf->wifi_passwd, args, sizeof(conf->wifi_passwd));
	}
	return 0;
}

int cmd_time(const char *cmd, const char *args, int query, char *prev_cmd)
{
	datetime_t t;
	time_t tnow;
	char buf[64];

	if (query && rtc_get_datetime(&t)) {
		tnow = datetime_to_time(&t);
		strftime(buf, sizeof(buf), "%a, %d %b %Y %T %z %Z", localtime(&tnow));
		printf("%s\n", buf);
	}

	return 0;
}

struct cmd_t wifi_commands[] = {
	{ "COUntry",   3, NULL,              cmd_wifi_country },
	{ "IPaddress", 2, NULL,              cmd_wifi_ip },
	{ "MAC",       3, NULL,              cmd_wifi_mac },
	{ "PASSword",  4, NULL,              cmd_wifi_password },
	{ "SSID",      4, NULL,              cmd_wifi_ssid },
	{ "STATus",    4, NULL,              cmd_wifi_status },
	{ 0, 0, 0, 0 }
};

struct cmd_t system_commands[] = {
	{ "DEBug",     5, NULL,              cmd_debug },
	{ "ECHO",      4, NULL,              cmd_echo },
	{ "FANS",      4, NULL,              cmd_fans },
	{ "LED",       3, NULL,              cmd_led },
	{ "MBFANS",    6, NULL,              cmd_mbfans },
	{ "SENSORS",   7, NULL,              cmd_sensors },
	{ "UPGRADE",   7, NULL,              cmd_usb_boot },
	{ "VERsion",   3, NULL,              cmd_version },
	{ "DISPlay",   4, NULL,              cmd_display_type },
	{ "WIFI",      4, wifi_commands,     cmd_wifi },
	{ "TIME",      4, NULL,              cmd_time },
	{ 0, 0, 0, 0 }
};

struct cmd_t fan_c_commands[] = {
	{ "NAME",      4, NULL,              cmd_fan_name },
	{ "MINpwm",    3, NULL,              cmd_fan_min_pwm },
	{ "MAXpwm",    3, NULL,              cmd_fan_max_pwm },
	{ "PWMCoeff",  4, NULL,              cmd_fan_pwm_coef },
	{ "RPMFactor", 4, NULL,              cmd_fan_rpm_factor },
	{ "SOUrce",    3, NULL,              cmd_fan_source },
	{ "PWMMap",    4, NULL,              cmd_fan_pwm_map },
	{ 0, 0, 0, 0 }
};

struct cmd_t mbfan_c_commands[] = {
	{ "NAME",      4, NULL,              cmd_mbfan_name },
	{ "MINrpm",    3, NULL,              cmd_mbfan_min_rpm },
	{ "MAXrpm",    3, NULL,              cmd_mbfan_max_rpm },
	{ "RPMCoeff",  4, NULL,              cmd_mbfan_rpm_coef },
	{ "RPMFactor", 4, NULL,              cmd_mbfan_rpm_factor },
	{ "SOUrce",    3, NULL,              cmd_mbfan_source },
	{ "RPMMap",    4, NULL,              cmd_mbfan_rpm_map },
	{ 0, 0, 0, 0 }
};

struct cmd_t sensor_c_commands[] = {
	{ "NAME",        4, NULL,             cmd_sensor_name },
	{ "TEMPOffset",  5, NULL,             cmd_sensor_temp_offset },
	{ "TEMPCoeff",   5, NULL,             cmd_sensor_temp_coef },
	{ "TEMPMap",     5, NULL,             cmd_sensor_temp_map },
	{ "TEMPNominal", 5, NULL,             cmd_sensor_temp_nominal },
	{ "THERmistor",  4, NULL,             cmd_sensor_ther_nominal },
	{ "BETAcoeff",   4, NULL,             cmd_sensor_beta_coef },
	{ 0, 0, 0, 0 }
};

struct cmd_t config_commands[] = {
	{ "SAVe",      3, NULL,              cmd_save_config },
	{ "Read",      1, NULL,              cmd_print_config },
	{ "DELete",    3, NULL,              cmd_delete_config },
	{ "FAN",       3, fan_c_commands,    NULL },
	{ "MBFAN",     5, mbfan_c_commands,  NULL },
	{ "SENSOR",    6, sensor_c_commands, NULL },
	{ 0, 0, 0, 0 }
};

struct cmd_t fan_commands[] = {
	{ "RPM",       3, NULL,              cmd_fan_rpm },
	{ "PWM",       3, NULL,              cmd_fan_pwm },
	{ "TACho",     3, NULL,              cmd_fan_tacho },
	{ "Read",      1, NULL,              cmd_fan_read },
	{ 0, 0, 0, 0 }
};

struct cmd_t mbfan_commands[] = {
	{ "RPM",       3, NULL,              cmd_mbfan_rpm },
	{ "PWM",       3, NULL,              cmd_mbfan_pwm },
	{ "TACho",     3, NULL,              cmd_mbfan_tacho },
	{ "Read",      1, NULL,              cmd_mbfan_read },
	{ 0, 0, 0, 0 }
};

struct cmd_t sensor_commands[] = {
	{ "TEMP",      4, NULL,              cmd_sensor_temp },
	{ "Read",      1, NULL,              cmd_sensor_temp },
	{ 0, 0, 0, 0 }
};

struct cmd_t measure_commands[] = {
	{ "Read",      1, NULL,              cmd_read },
	{ "FAN",       3, fan_commands,      cmd_fan_read },
	{ "MBFAN",     5, mbfan_commands,    cmd_mbfan_read },
	{ "SENSOR",    6, sensor_commands,   cmd_sensor_temp },
	{ 0, 0, 0, 0 }
};

struct cmd_t commands[] = {
	{ "*CLS",      4, NULL,              cmd_null },
	{ "*ESE",      4, NULL,              cmd_null },
	{ "*ESR",      4, NULL,              cmd_zero },
	{ "*IDN",      4, NULL,              cmd_idn },
	{ "*OPC",      4, NULL,              cmd_one },
	{ "*RST",      4, NULL,              cmd_reset },
	{ "*SRE",      4, NULL,              cmd_zero },
	{ "*STB",      4, NULL,              cmd_zero },
	{ "*TST",      4, NULL,              cmd_zero },
	{ "*WAI",      4, NULL,              cmd_null },
	{ "CONFigure", 4, config_commands,   cmd_print_config },
	{ "MEAsure",   3, measure_commands,  NULL },
	{ "SYStem",    3, system_commands,   NULL },
	{ "Read",      1, NULL,              cmd_read },
	{ 0, 0, 0, 0 }
};



struct cmd_t* run_cmd(char *cmd, struct cmd_t *cmd_level, char **prev_subcmd)
{
	int i, query, cmd_len, total_len;
	char *saveptr1, *saveptr2, *t, *sub, *s, *arg;
	int res = -1;

	total_len = strlen(cmd);
	t = strtok_r(cmd, " \t", &saveptr1);
	if (t && strlen(t) > 0) {
		cmd_len = strlen(t);
		if (*t == ':' || *t == '*') {
			/* reset command level to 'root' */
			cmd_level = commands;
			*prev_subcmd = NULL;
		}
		/* Split command to subcommands and search from command tree ... */
		sub = strtok_r(t, ":", &saveptr2);
		while (sub && strlen(sub) > 0) {
			s = sub;
			sub = NULL;
			i = 0;
			while (cmd_level[i].cmd) {
				if (!strncasecmp(s, cmd_level[i].cmd, cmd_level[i].min_match)) {
					sub = strtok_r(NULL, ":", &saveptr2);
					if (cmd_level[i].subcmds && sub && strlen(sub) > 0) {
						/* Match for subcommand...*/
						*prev_subcmd = s;
						cmd_level = cmd_level[i].subcmds;
					} else if (cmd_level[i].func) {
						/* Match for command */
						query = (s[strlen(s)-1] == '?' ? 1 : 0);
						arg = t + cmd_len + 1;
						res = cmd_level[i].func(s,
								(total_len > cmd_len+1 ? arg : ""),
								query,
								(*prev_subcmd ? *prev_subcmd : ""));
					}
					break;
				}
				i++;
			}
		}
	}

	if (res < 0)
		debug(1,"Unknown command.\n");

	return cmd_level;
}


void process_command(struct fanpico_state *state, struct fanpico_config *config, char *command)
{
	char *saveptr, *cmd;
	char *prev_subcmd = NULL;
	struct cmd_t *cmd_level = commands;

	if (!state || !config || !command)
		return;

	st = state;
	conf = config;

	cmd = strtok_r(command, ";", &saveptr);
	while (cmd) {
		cmd = trim_str(cmd);
		debug(1, "command: '%s'\n", cmd);
		if (cmd && strlen(cmd) > 0) {
			cmd_level = run_cmd(cmd, cmd_level, &prev_subcmd);
		}
		cmd = strtok_r(NULL, ";", &saveptr);
	}
}
