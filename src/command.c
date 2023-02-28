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
#include <ctype.h>
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
#ifdef WIFI_SUPPORT
#include "lwip/ip_addr.h"
#include "lwip/stats.h"
#endif


struct cmd_t {
	const char   *cmd;
	uint8_t       min_match;
	struct cmd_t *subcmds;
	int (*func)(const char *cmd, const char *args, int query, char *prev_cmd);
};

struct error_t {
	const char    *error;
	int            error_num;
};

struct error_t error_codes[] = {
	{ "No Error", 0 },
	{ "Command Error", -100 },
	{ "Syntax Error", -102 },
	{ "Undefined Header", -113 },
	{ NULL, 0 }
};

int last_error_num = 0;

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
	char buf[64];
	const char *msg[] = {
		"FIRMWARE UPGRADE MODE",
		"=====================",
		"Use file (.uf2):",
		buf,
		"",
		"Copy file to: RPI-RP2",
		"",
		"Press RESET to abort.",
	};

	if (query)
		return 1;

	snprintf(buf, sizeof(buf), " fanpico-%s-%s", FANPICO_MODEL, PICO_BOARD);
	display_message(8, msg);

	reset_usb_boot(0, 0);
	return 0; /* should never get this far... */
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
	} else if (str_to_int(args, &mode, 10)) {
		if (mode >= 0 && mode <= 2) {
			log_msg(LOG_NOTICE, "Set system LED mode: %d -> %d", cfg->led_mode, mode);
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
	log_msg(LOG_INFO, "null command: %s %s (query=%d)", cmd, args, query);
	return 0;
}

int cmd_debug(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int level;

	if (query) {
		printf("%d\n", get_debug_level());
	} else if (str_to_int(args, &level, 10)) {
		set_debug_level((level < 0 ? 0 : level));
	}
	return 0;
}

int cmd_log_level(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int level;

	if (query) {
		printf("%d\n", get_log_level());
	} else if (str_to_int(args, &level, 10)) {
	        level = (level < 0 ? 0 : level);
		log_msg(LOG_NOTICE, "Change log level: %d -> %d", get_log_level(), level);
		set_log_level(level);
	}
	return 0;
}

int cmd_syslog_level(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int level;

	if (query) {
		printf("%d\n", get_syslog_level());
	} else if (str_to_int(args, &level, 10)) {
	        level = (level < 0 ? 0 : level);
		log_msg(LOG_NOTICE, "Change syslog level: %d -> %d", get_syslog_level(), level);
		set_syslog_level(level);
	}
	return 0;
}

int cmd_echo(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int val;

	if (query) {
		printf("%u\n", cfg->local_echo);
	} else if (str_to_int(args, &val, 10)) {
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
	const char *msg[] = {
		"    Rebooting...",
	};

	if (query)
		return 1;

	log_msg(LOG_ALERT, "Initiating reboot...");
	display_message(1, msg);

	watchdog_disable();
	sleep_ms(500);
	watchdog_reboot(0, SRAM_END, 1);
	while (1);

	/* Should never get this far... */
	return 0;
}

int cmd_save_config(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query)
		return 1;
	save_config();
	return 0;
}

int cmd_print_config(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (!query)
		return 1;
	print_config();
	return 0;
}

int cmd_delete_config(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query)
		return 1;
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
		return 1;

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
			log_msg(LOG_NOTICE, "fan%d: change name '%s' --> '%s'", fan + 1,
				conf->fans[fan].name, args);
			strncopy(conf->fans[fan].name, args, sizeof(conf->fans[fan].name));
		}
		return 0;
	}
	return 1;
}

int cmd_fan_min_pwm(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan, val;

	fan = atoi(&prev_cmd[3]) - 1;
	if (fan >= 0 && fan < FAN_COUNT) {
		if (query) {
			printf("%d\n", conf->fans[fan].min_pwm);
		} else if (str_to_int(args, &val, 10)) {
			if (val >= 0 && val <= 100) {
				log_msg(LOG_NOTICE, "fan%d: change min PWM %d%% --> %d%%", fan + 1,
					conf->fans[fan].min_pwm, val);
				conf->fans[fan].min_pwm = val;
			} else {
				log_msg(LOG_WARNING, "fan%d: invalid new value for min PWM: %d", fan + 1,
					val);
				return 2;
			}
		}
		return 0;
	}
	return 1;
}

int cmd_fan_max_pwm(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan, val;

	fan = atoi(&prev_cmd[3]) - 1;
	if (fan >= 0 && fan < FAN_COUNT) {
		if (query) {
			printf("%d\n", conf->fans[fan].max_pwm);
		} else if (str_to_int(args, &val, 10)) {
			if (val >= 0 && val <= 100) {
				log_msg(LOG_NOTICE, "fan%d: change max PWM %d%% --> %d%%", fan + 1,
					conf->fans[fan].max_pwm, val);
				conf->fans[fan].max_pwm = val;
			} else {
				log_msg(LOG_WARNING, "fan%d: invalid new value for max PWM: %d", fan + 1,
					val);
				return 2;
			}
		}
		return 0;
	}
	return 1;
}

int cmd_fan_pwm_coef(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	float val;

	fan = atoi(&prev_cmd[3]) - 1;
	if (fan >= 0 && fan < FAN_COUNT) {
		if (query) {
			printf("%f\n", conf->fans[fan].pwm_coefficient);
		} else if (str_to_float(args, &val)) {
			if (val >= 0.0) {
				log_msg(LOG_NOTICE, "fan%d: change PWM coefficient %f --> %f",
					fan + 1, conf->fans[fan].pwm_coefficient, val);
				conf->fans[fan].pwm_coefficient = val;
			} else {
				log_msg(LOG_WARNING, "fan%d: invalid new value for PWM coefficient: %f",
					fan + 1, val);
				return 2;
			}
		}
		return 0;
	}
	return 1;
}

int cmd_fan_pwm_map(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan, i, count;
	int val;
	char *arg, *t, *saveptr;
	struct pwm_map *map;
	struct pwm_map new_map;
	int ret = 0;

	fan = atoi(&prev_cmd[3]) - 1;
	if (fan < 0 || fan >= FAN_COUNT)
		return 1;
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
			log_msg(LOG_WARNING, "fan%d: invalid new map: %s", fan + 1, args);
			ret = 2;
		}
		free(arg);
	}

	return ret;
}

int cmd_fan_filter(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	int ret = 0;
	char *tok, *saveptr, *param;
	struct fan_output *f;
	enum signal_filter_types new_filter;
	void *new_ctx;

	fan = atoi(&prev_cmd[3]) - 1;
	if (fan < 0 || fan >= FAN_COUNT)
		return 1;

	f = &conf->fans[fan];
	if (query) {
		printf("%s", filter2str(f->filter));
		tok = filter_print_args(f->filter, f->filter_ctx);
		if (tok) {
			printf(",%s\n", tok);
			free(tok);
		} else {
			printf(",\n");
		}
	} else {
		param = strdup(args);
		if ((tok = strtok_r(param, ",", &saveptr)) != NULL) {
			new_filter = str2filter(tok);
			tok += strlen(tok) + 1;
			new_ctx = filter_parse_args(new_filter, tok);
			if (new_filter == FILTER_NONE || new_ctx != NULL) {
				f->filter = new_filter;
				if (f->filter_ctx)
					free(f->filter_ctx);
				f->filter_ctx = new_ctx;
			} else {
				ret = 1;
			}
		}
		free(param);
	}

	return ret;
}

int cmd_fan_rpm_factor(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	int val;

	fan = atoi(&prev_cmd[3]) - 1;
	if (fan >= 0 && fan < FAN_COUNT) {
		if (query) {
			printf("%u\n", conf->fans[fan].rpm_factor);
		} else if (str_to_int(args, &val, 10)) {
			if (val >= 1 || val <= 8) {
				log_msg(LOG_NOTICE, "fan%d: change RPM factor %u --> %d",
					fan + 1, conf->fans[fan].rpm_factor, val);
				conf->fans[fan].rpm_factor = val;
			} else {
				log_msg(LOG_WARNING, "fan%d: invalid new value for RPM factor: %d",
					fan + 1, val);
				return 2;
			}
		}
		return 0;
	}
	return 1;
}

int cmd_fan_source(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	int type, val, d_o, d_n;
	char *tok, *saveptr, *param;
	int ret = 0;

	fan = atoi(&prev_cmd[3]) - 1;
	if (fan < 0 || fan >= FAN_COUNT)
		return 1;

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
					log_msg(LOG_NOTICE, "fan%d: change source %s,%u --> %s,%u",
						fan + 1,
						pwm_source2str(conf->fans[fan].s_type),
						conf->fans[fan].s_id + d_o,
						pwm_source2str(type),
						val + d_n);
					conf->fans[fan].s_type = type;
					conf->fans[fan].s_id = val;
				} else {
					log_msg(LOG_WARNING, "fan%d: invalid source: %s",
						fan + 1, args);
					ret = 2;
				}
			}
		}
		free(param);
	}

	return ret;
}

int cmd_fan_rpm(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	double rpm;

	if (!query)
		return 1;

	fan = atoi(&prev_cmd[3]) - 1;
	if (fan >= 0 && fan < FAN_COUNT) {
		rpm = st->fan_freq[fan] * 60.0 / conf->fans[fan].rpm_factor;
		log_msg(LOG_DEBUG, "fan%d (tacho = %fHz) rpm = %.1lf", fan + 1,
			st->fan_freq[fan], rpm);
		printf("%.0lf\n", rpm);
		return 0;
	}

	return 1;
}

int cmd_fan_tacho(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	float f;

	if (!query)
		return 1;

	fan = atoi(&prev_cmd[3]) - 1;
	if (fan >= 0 && fan < FAN_COUNT) {
		f = st->fan_freq[fan];
		log_msg(LOG_DEBUG, "fan%d tacho = %fHz", fan + 1, f);
		printf("%.1f\n", f);
		return 0;
	}

	return 1;
}

int cmd_fan_pwm(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	float d;

	if (!query)
		return 1;

	fan = atoi(&prev_cmd[3]) - 1;
	if (fan >= 0 && fan < FAN_COUNT) {
		d = st->fan_duty[fan];
		log_msg(LOG_DEBUG, "fan%d duty = %f%%", fan + 1, d);
		printf("%.0f\n", d);
		return 0;
	}

	return 1;
}

int cmd_fan_read(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	double rpm;
	float d, f;

	if (!query)
		return 1;

	if (!strncasecmp(prev_cmd, "fan", 3)) {
		fan = atoi(&prev_cmd[3]) - 1;
	} else {
		fan = atoi(&cmd[3]) - 1;
	}

	if (fan >= 0 && fan < FAN_COUNT) {
		d = st->fan_duty[fan];
		f = st->fan_freq[fan];
		rpm = f * 60.0 / conf->fans[fan].rpm_factor;
		log_msg(LOG_DEBUG, "fan%d duty = %f%%, freq = %fHz, speed = %fRPM",
			fan + 1, d, f, rpm);
		printf("%.0f,%.1f,%.0f\n", d, f, rpm);
		return 0;
	}

	return 1;
}

int cmd_mbfan_name(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int mbfan;

	mbfan = atoi(&prev_cmd[5]) - 1;
	if (mbfan >= 0 && mbfan < MBFAN_COUNT) {
		if (query) {
			printf("%s\n", conf->mbfans[mbfan].name);
		} else {
			log_msg(LOG_NOTICE, "mbfan%d: change name '%s' --> '%s'", mbfan + 1,
				conf->mbfans[mbfan].name, args);
			strncopy(conf->mbfans[mbfan].name, args, sizeof(conf->mbfans[mbfan].name));
		}
		return 0;
	}
	return 1;
}

int cmd_mbfan_min_rpm(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan, val;

	fan = atoi(&prev_cmd[5]) - 1;
	if (fan >= 0 && fan < MBFAN_COUNT) {
		if (query) {
			printf("%d\n", conf->mbfans[fan].min_rpm);
		} else if (str_to_int(args, &val, 10)) {
			if (val >= 0 && val <= 50000) {
				log_msg(LOG_NOTICE, "mbfan%d: change min RPM %d --> %d", fan + 1,
					conf->mbfans[fan].min_rpm, val);
				conf->mbfans[fan].min_rpm = val;
			} else {
				log_msg(LOG_WARNING, "mbfan%d: invalid new value for min RPM: %d", fan + 1,
					val);
				return 2;
			}
		}
		return 0;
	}
	return 1;
}

int cmd_mbfan_max_rpm(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan, val;

	fan = atoi(&prev_cmd[5]) - 1;
	if (fan >= 0 && fan < MBFAN_COUNT) {
		if (query) {
			printf("%d\n", conf->mbfans[fan].max_rpm);
		} else if (str_to_int(args, &val, 10)) {
			if (val >= 0 && val <= 50000) {
				log_msg(LOG_NOTICE, "mbfan%d: change max RPM %d --> %d", fan + 1,
					conf->mbfans[fan].max_rpm, val);
				conf->mbfans[fan].max_rpm = val;
			} else {
				log_msg(LOG_WARNING, "fan%d: invalid new value for max RPM: %d", fan + 1,
					val);
				return 2;
			}
		}
		return 0;
	}
	return 1;
}

int cmd_mbfan_rpm_coef(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	float val;

	fan = atoi(&prev_cmd[5]) - 1;
	if (fan >= 0 && fan < MBFAN_COUNT) {
		if (query) {
			printf("%f\n", conf->mbfans[fan].rpm_coefficient);
		} else if (str_to_float(args, &val)) {
			if (val > 0.0) {
				log_msg(LOG_NOTICE, "mbfan%d: change RPM coefficient %f --> %f",
					fan + 1, conf->mbfans[fan].rpm_coefficient, val);
				conf->mbfans[fan].rpm_coefficient = val;
			} else {
				log_msg(LOG_WARNING, "mbfan%d: invalid new value for RPM coefficient: %f",
					fan + 1, val);
				return 2;
			}
		}
		return 0;
	}
	return 1;
}

int cmd_mbfan_rpm_factor(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	int val;

	fan = atoi(&prev_cmd[5]) - 1;
	if (fan >= 0 && fan < MBFAN_COUNT) {
		if (query) {
			printf("%u\n", conf->mbfans[fan].rpm_factor);
		} else if (str_to_int(args, &val, 10)) {
			if (val >= 1 || val <= 8) {
				log_msg(LOG_NOTICE, "mbfan%d: change RPM factor %u --> %d",
					fan + 1, conf->mbfans[fan].rpm_factor, val);
				conf->mbfans[fan].rpm_factor = val;
			} else {
				log_msg(LOG_WARNING, "mbfan%d: invalid new value for RPM factor: %d",
					fan + 1, val);
				return 2;
			}
		}
		return 0;
	}
	return 1;
}

int cmd_mbfan_rpm_map(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan, i, count;
	int val;
	char *arg, *t, *saveptr;
	struct tacho_map *map;
	struct tacho_map new_map;
	int ret = 0;

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
			log_msg(LOG_WARNING, "mbfan%d: invalid new map: %s", fan + 1, args);
			ret = 2;
		}
		free(arg);
	}

	return ret;
}

int cmd_mbfan_source(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	int type, val, d_o, d_n;
	char *tok, *saveptr, *param;
	int ret = 0;

	fan = atoi(&prev_cmd[5]) - 1;
	if (fan < 0 || fan >= MBFAN_COUNT)
		return 1;

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
					log_msg(LOG_NOTICE, "mbfan%d: change source %s,%u --> %s,%u",
						fan + 1,
						tacho_source2str(conf->mbfans[fan].s_type),
						conf->mbfans[fan].s_id + d_o,
						tacho_source2str(type),
						val + d_n);
					conf->mbfans[fan].s_type = type;
					conf->mbfans[fan].s_id = val;
				} else {
					log_msg(LOG_WARNING, "mbfan%d: invalid source: %s",
						fan + 1, args);
					ret = 2;
				}
			}
		}
		free(param);
	}

	return ret;
}

int cmd_mbfan_rpm(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	double rpm;

	if (query) {
		fan = atoi(&prev_cmd[5]) - 1;
		if (fan >= 0 && fan < MBFAN_COUNT) {
			rpm = st->mbfan_freq[fan] * 60.0 / conf->mbfans[fan].rpm_factor;
			log_msg(LOG_DEBUG, "mbfan%d (tacho = %fHz) rpm = %.1lf", fan+1,
				st->mbfan_freq[fan], rpm);
			printf("%.0lf\n", rpm);
			return 0;
		}
	}
	return 1;
}

int cmd_mbfan_tacho(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	float f;

	if (query) {
		fan = atoi(&prev_cmd[5]) - 1;
		if (fan >= 0 && fan < MBFAN_COUNT) {
			f = st->mbfan_freq[fan];
			log_msg(LOG_DEBUG, "mbfan%d tacho = %fHz", fan + 1, f);
			printf("%.1f\n", f);
			return 0;
		}
	}
	return 1;
}

int cmd_mbfan_pwm(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	float d;

	if (query) {
		fan = atoi(&prev_cmd[5]) - 1;
		if (fan >= 0 && fan < MBFAN_COUNT) {
			d = st->mbfan_duty[fan];
			log_msg(LOG_DEBUG, "mbfan%d duty = %f%%", fan + 1, d);
			printf("%.0f\n", d);
			return 0;
		}
	}
	return 1;
}

int cmd_mbfan_read(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	double rpm;
	float d, f;

	if (!query)
		return 1;

	if (!strncasecmp(prev_cmd, "mbfan", 5)) {
		fan = atoi(&prev_cmd[5]) - 1;
	} else {
		fan = atoi(&cmd[5]) - 1;
	}

	if (fan >= 0 && fan < MBFAN_COUNT) {
		d = st->mbfan_duty[fan];
		f = st->mbfan_freq[fan];
		rpm = f * 60.0 / conf->mbfans[fan].rpm_factor;
		log_msg(LOG_DEBUG, "mbfan%d duty = %f%%, freq = %fHz, speed = %fRPM",
			fan + 1, d, f, rpm);
		printf("%.0f,%.1f,%.0f\n", d, f, rpm);
		return 0;
	}

	return 1;
}

int cmd_mbfan_filter(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int mbfan;
	int ret = 0;
	char *tok, *saveptr, *param;
	struct mb_input *m;
	enum signal_filter_types new_filter;
	void *new_ctx;

	mbfan = atoi(&prev_cmd[5]) - 1;
	if (mbfan < 0 || mbfan >= MBFAN_COUNT)
		return 1;

	m = &conf->mbfans[mbfan];
	if (query) {
		printf("%s", filter2str(m->filter));
		tok = filter_print_args(m->filter, m->filter_ctx);
		if (tok) {
			printf(",%s\n", tok);
			free(tok);
		} else {
			printf(",\n");
		}
	} else {
		param = strdup(args);
		if ((tok = strtok_r(param, ",", &saveptr)) != NULL) {
			new_filter = str2filter(tok);
			tok += strlen(tok) + 1;
			new_ctx = filter_parse_args(new_filter, tok);
			if (new_filter == FILTER_NONE || new_ctx != NULL) {
				m->filter = new_filter;
				if (m->filter_ctx)
					free(m->filter_ctx);
				m->filter_ctx = new_ctx;
			} else {
				ret = 1;
			}
		}
		free(param);
	}

	return ret;
}

int cmd_sensor_name(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor;

	sensor = atoi(&prev_cmd[6]) - 1;
	if (sensor >= 0 && sensor < SENSOR_COUNT) {
		if (query) {
			printf("%s\n", conf->sensors[sensor].name);
		} else {
			log_msg(LOG_NOTICE, "sensor%d: change name '%s' --> '%s'", sensor + 1,
				conf->sensors[sensor].name, args);
			strncopy(conf->sensors[sensor].name, args,
				sizeof(conf->sensors[sensor].name));
		}
		return 0;
	}
	return 1;
}

int cmd_sensor_temp_offset(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor;
	float val;

	sensor = atoi(&prev_cmd[6]) - 1;
	if (sensor >= 0 && sensor < SENSOR_COUNT) {
		if (query) {
			printf("%f\n", conf->sensors[sensor].temp_offset);
		} else if (str_to_float(args, &val)) {
			log_msg(LOG_NOTICE, "sensor%d: change temp offset %f --> %f", sensor + 1,
				conf->sensors[sensor].temp_offset, val);
			conf->sensors[sensor].temp_offset = val;
		}
		return 0;
	}
	return 1;
}

int cmd_sensor_temp_coef(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor;
	float val;

	sensor = atoi(&prev_cmd[6]) - 1;
	if (sensor >= 0 && sensor < SENSOR_COUNT) {
		if (query) {
			printf("%f\n", conf->sensors[sensor].temp_coefficient);
		} else if (str_to_float(args, &val)) {
			if (val > 0.0) {
				log_msg(LOG_NOTICE, "sensor%d: change temp coefficient %f --> %f",
					sensor + 1, conf->sensors[sensor].temp_coefficient,
					val);
				conf->sensors[sensor].temp_coefficient = val;
			} else {
				log_msg(LOG_WARNING, "sensor%d: invalid temp coefficient: %f",
					sensor + 1, val);
			}
		}
		return 0;
	}
	return 1;
}

int cmd_sensor_temp_nominal(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor;
	float val;

	sensor = atoi(&prev_cmd[6]) - 1;
	if (sensor >= 0 && sensor < SENSOR_COUNT) {
		if (query) {
			printf("%.1f\n", conf->sensors[sensor].temp_nominal);
		} else if (str_to_float(args, &val)) {
			if (val >= -50.0 && val <= 100.0) {
				log_msg(LOG_NOTICE, "sensor%d: change temp nominal %.1fC --> %.1fC",
					sensor + 1, conf->sensors[sensor].temp_nominal,
					val);
				conf->sensors[sensor].temp_nominal = val;
			} else {
				log_msg(LOG_WARNING, "sensor%d: invalid temp nominal: %f",
					sensor + 1, val);
				return 2;
			}
		}
		return 0;
	}
	return 1;
}

int cmd_sensor_ther_nominal(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor;
	float val;

	sensor = atoi(&prev_cmd[6]) - 1;
	if (sensor >= 0 && sensor < SENSOR_COUNT) {
		if (query) {
			printf("%.0f\n", conf->sensors[sensor].thermistor_nominal);
		} else if (str_to_float(args, &val)) {
			if (val > 0.0) {
				log_msg(LOG_NOTICE, "sensor%d: change thermistor nominal %.0f ohm --> %.0f ohm",
					sensor + 1, conf->sensors[sensor].thermistor_nominal,
					val);
				conf->sensors[sensor].thermistor_nominal = val;
			} else {
				log_msg(LOG_WARNING, "sensor%d: invalid thermistor nominal: %f",
					sensor + 1, val);
				return 2;
			}
		}
		return 0;
	}
	return 1;
}

int cmd_sensor_beta_coef(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor;
	float val;

	sensor = atoi(&prev_cmd[6]) - 1;
	if (sensor >= 0 && sensor < SENSOR_COUNT) {
		if (query) {
			printf("%.0f\n", conf->sensors[sensor].beta_coefficient);
		} else if (str_to_float(args, &val)) {
			if (val > 0.0) {
				log_msg(LOG_NOTICE, "sensor%d: change thermistor beta coefficient %.0f --> %.0f",
					sensor + 1, conf->sensors[sensor].beta_coefficient,
					val);
				conf->sensors[sensor].beta_coefficient = val;
			} else {
				log_msg(LOG_WARNING, "sensor%d: invalid thermistor beta coefficient: %f",
					sensor + 1, val);
				return 2;
			}
		}
		return 0;
	}
	return 1;
}

int cmd_sensor_temp_map(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor, i, count;
	float val;
	char *arg, *t, *saveptr;
	struct temp_map *map;
	struct temp_map new_map;
	int ret = 0;

	sensor = atoi(&prev_cmd[6]) - 1;
	if (sensor < 0 || sensor >= SENSOR_COUNT)
		return 1;
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
			log_msg(LOG_WARNING, "sensor%d: invalid new map: %s", sensor + 1, args);
			ret = 2;
		}
		free(arg);
	}

	return ret;
}

int cmd_sensor_temp(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor;
	float d;

	if (!query)
		return 1;

	if (!strncasecmp(prev_cmd, "sensor", 6)) {
		sensor = atoi(&prev_cmd[6]) - 1;
	} else {
		sensor = atoi(&cmd[6]) - 1;
	}

	if (sensor >= 0 && sensor < SENSOR_COUNT) {
		d = st->temp[sensor];
		log_msg(LOG_DEBUG, "sensor%d temperature = %fC", sensor + 1, d);
		printf("%.0f\n", d);
		return 0;
	}

	return 1;
}

int cmd_sensor_filter(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor;
	int ret = 0;
	char *tok, *saveptr, *param;
	struct sensor_input *s;
	enum signal_filter_types new_filter;
	void *new_ctx;

	sensor = atoi(&prev_cmd[6]) - 1;
	if (sensor < 0 || sensor >= SENSOR_COUNT)
		return 1;

	s = &conf->sensors[sensor];
	if (query) {
		printf("%s", filter2str(s->filter));
		tok = filter_print_args(s->filter, s->filter_ctx);
		if (tok) {
			printf(",%s\n", tok);
			free(tok);
		} else {
			printf(",\n");
		}
	} else {
		param = strdup(args);
		if ((tok = strtok_r(param, ",", &saveptr)) != NULL) {
			new_filter = str2filter(tok);
			tok += strlen(tok) + 1;
			new_ctx = filter_parse_args(new_filter, tok);
			if (new_filter == FILTER_NONE || new_ctx != NULL) {
				s->filter = new_filter;
				if (s->filter_ctx)
					free(s->filter_ctx);
				s->filter_ctx = new_ctx;
			} else {
				ret = 1;
			}
		}
		free(param);
	}

	return ret;
}

int cmd_wifi(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query) {
#ifdef WIFI_SUPPORT
		printf("1\n");
#else
		printf("0\n");
#endif
		return 0;
	}
	return 1;
}

#ifdef WIFI_SUPPORT
int ip_change(const char *cmd, const char *args, int query, char *prev_cmd, const char *name, ip_addr_t *ip)
{
	ip_addr_t tmpip;

	if (query) {
		printf("%s\n", ipaddr_ntoa(ip));
	} else {
		if (!ipaddr_aton(args, &tmpip))
			return 2;
		log_msg(LOG_NOTICE, "%s change '%s' --> %s'", name, ipaddr_ntoa(ip), args);
		ip_addr_copy(*ip, tmpip);
	}
	return 0;
}

int cmd_wifi_ip(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return ip_change(cmd, args, query, prev_cmd, "IP", &cfg->ip);
}

int cmd_wifi_netmask(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return ip_change(cmd, args, query, prev_cmd, "Netmask", &cfg->netmask);
}

int cmd_wifi_gateway(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return ip_change(cmd, args, query, prev_cmd, "Default Gateway", &cfg->gateway);
}

int cmd_wifi_syslog(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return ip_change(cmd, args, query, prev_cmd, "Syslog Server", &cfg->syslog_server);
}

int cmd_wifi_ntp(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return ip_change(cmd, args, query, prev_cmd, "NTP Server", &cfg->ntp_server);
}

int cmd_wifi_mac(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query) {
		network_mac();
		return 0;
	}
	return 1;
}

int cmd_wifi_ssid(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query) {
		printf("%s\n", conf->wifi_ssid);
	} else {
		log_msg(LOG_NOTICE, "Wi-Fi SSID change '%s' --> '%s'",
			conf->wifi_ssid, args);
		strncopy(conf->wifi_ssid, args, sizeof(conf->wifi_ssid));
	}
	return 0;
}

int cmd_wifi_status(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query) {
		network_status();
		return 0;
	}
	return 1;
}

int cmd_wifi_stats(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query) {
		stats_display();
		return 0;
	}
	return 1;
}

int cmd_wifi_country(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query) {
		printf("%s\n", conf->wifi_country);
	} else {
		if (valid_wifi_country(args)) {
			log_msg(LOG_NOTICE, "Wi-Fi Country change '%s' --> '%s'",
				conf->wifi_country, args);
			strncopy(conf->wifi_country, args, sizeof(conf->wifi_country));
		} else {
			log_msg(LOG_WARNING, "Invalid Wi-Fi country: %s", args);
			return 2;
		}
	}
	return 0;
}

int cmd_wifi_password(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query) {
		printf("%s\n", conf->wifi_passwd);
	} else {
		log_msg(LOG_NOTICE, "Wi-Fi Password change '%s' --> '%s'",
			conf->wifi_passwd, args);
		strncopy(conf->wifi_passwd, args, sizeof(conf->wifi_passwd));
	}
	return 0;
}

int cmd_wifi_hostname(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query) {
		printf("%s\n", conf->hostname);
	} else {
		for (int i = 0; i < strlen(args); i++) {
			if (!(isalpha((int)args[i]) || args[i] == '-')) {
				return 1;
			}
		}
		log_msg(LOG_NOTICE, "System hostname change '%s' --> '%s'", conf->hostname, args);
		strncopy(conf->hostname, args, sizeof(conf->hostname));
	}
	return 0;
}
#endif

int cmd_time(const char *cmd, const char *args, int query, char *prev_cmd)
{
	datetime_t t;
	time_t tnow;
	char buf[64];

	if (!query)
		return 1;

	if (rtc_get_datetime(&t)) {
		tnow = datetime_to_time(&t);
		strftime(buf, sizeof(buf), "%a, %d %b %Y %T %z %Z", localtime(&tnow));
		printf("%s\n", buf);
	}

	return 0;
}

int cmd_uptime(const char *cmd, const char *args, int query, char *prev_cmd)
{
	uint32_t secs = to_us_since_boot(get_absolute_time()) / 1000000;
	uint32_t mins =  secs / 60;
	uint32_t hours = mins / 60;
	uint32_t days = hours / 24;

	if (!query)
		return 1;

	printf("up %lu days, %lu hours, %lu minutes\n", days, hours % 24, mins % 60);

	return 0;
}

int cmd_err(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (!query)
		return 1;

	for (int i = 0; error_codes[i].error != NULL; i++) {
		if (error_codes[i].error_num == last_error_num) {
			printf("%d,\"%s\"\n", last_error_num, error_codes[i].error);
			last_error_num = 0;
			return 0;
		}
	}
	printf("-1,\"Internal Error\"\n");
	return 0;
}

int cmd_name(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query) {
		printf("%s\n", conf->name);
	} else {
		log_msg(LOG_NOTICE, "System name change '%s' --> '%s'", conf->name, args);
		strncopy(conf->name, args, sizeof(conf->name));
	}
	return 0;
}

int cmd_memory(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int blocksize;

	if (query) {
		print_mallinfo();
		return 0;
	}
	if (str_to_int(args, &blocksize, 10)) {
		if (blocksize >= 512) {
			void *buf = NULL;
			size_t bufsize = blocksize;
			do {
				if (buf) {
					free(buf);
					bufsize += blocksize;
				}
				buf = malloc(bufsize);
			} while (buf);
			printf("Maximum available memory: %u bytes\n", bufsize - blocksize);
		}
		return 0;
	}
	return 1;
}


struct cmd_t wifi_commands[] = {
#ifdef WIFI_SUPPORT
	{ "COUntry",   3, NULL,              cmd_wifi_country },
	{ "GATEway",   4, NULL,              cmd_wifi_gateway },
	{ "HOSTname",  4, NULL,              cmd_wifi_hostname },
	{ "IPaddress", 2, NULL,              cmd_wifi_ip },
	{ "MAC",       3, NULL,              cmd_wifi_mac },
	{ "NETMask",   4, NULL,              cmd_wifi_netmask },
	{ "NTP",       3, NULL,              cmd_wifi_ntp },
	{ "PASSword",  4, NULL,              cmd_wifi_password },
	{ "SSID",      4, NULL,              cmd_wifi_ssid },
	{ "STATS",     5, NULL,              cmd_wifi_stats },
	{ "STATus",    4, NULL,              cmd_wifi_status },
	{ "SYSLOG",    6, NULL,              cmd_wifi_syslog },
#endif
	{ 0, 0, 0, 0 }
};

struct cmd_t system_commands[] = {
	{ "DEBUG",     5, NULL,              cmd_debug }, /* Obsolete ? */
	{ "DISPlay",   4, NULL,              cmd_display_type },
	{ "ECHO",      4, NULL,              cmd_echo },
	{ "ERRor",     3, NULL,              cmd_err },
	{ "FANS",      4, NULL,              cmd_fans },
	{ "LED",       3, NULL,              cmd_led },
	{ "LOG",       3, NULL,              cmd_log_level },
	{ "MBFANS",    6, NULL,              cmd_mbfans },
	{ "MEMory",    3, NULL,              cmd_memory },
	{ "NAME",      4, NULL,              cmd_name },
	{ "SENSORS",   7, NULL,              cmd_sensors },
	{ "SYSLOG",    6, NULL,              cmd_syslog_level },
	{ "TIME",      4, NULL,              cmd_time },
	{ "UPGRADE",   7, NULL,              cmd_usb_boot },
	{ "UPTIme",    4, NULL,              cmd_uptime },
	{ "VERsion",   3, NULL,              cmd_version },
	{ "WIFI",      4, wifi_commands,     cmd_wifi },
	{ 0, 0, 0, 0 }
};

struct cmd_t fan_c_commands[] = {
	{ "FILTER",    6, NULL,              cmd_fan_filter },
	{ "MAXpwm",    3, NULL,              cmd_fan_max_pwm },
	{ "MINpwm",    3, NULL,              cmd_fan_min_pwm },
	{ "NAME",      4, NULL,              cmd_fan_name },
	{ "PWMCoeff",  4, NULL,              cmd_fan_pwm_coef },
	{ "PWMMap",    4, NULL,              cmd_fan_pwm_map },
	{ "RPMFactor", 4, NULL,              cmd_fan_rpm_factor },
	{ "SOUrce",    3, NULL,              cmd_fan_source },
	{ 0, 0, 0, 0 }
};

struct cmd_t mbfan_c_commands[] = {
	{ "FILTER",    6, NULL,              cmd_mbfan_filter },
	{ "MAXrpm",    3, NULL,              cmd_mbfan_max_rpm },
	{ "MINrpm",    3, NULL,              cmd_mbfan_min_rpm },
	{ "NAME",      4, NULL,              cmd_mbfan_name },
	{ "RPMCoeff",  4, NULL,              cmd_mbfan_rpm_coef },
	{ "RPMFactor", 4, NULL,              cmd_mbfan_rpm_factor },
	{ "RPMMap",    4, NULL,              cmd_mbfan_rpm_map },
	{ "SOUrce",    3, NULL,              cmd_mbfan_source },
	{ 0, 0, 0, 0 }
};

struct cmd_t sensor_c_commands[] = {
	{ "BETAcoeff",   4, NULL,             cmd_sensor_beta_coef },
	{ "FILTER",      6, NULL,             cmd_sensor_filter },
	{ "NAME",        4, NULL,             cmd_sensor_name },
	{ "TEMPCoeff",   5, NULL,             cmd_sensor_temp_coef },
	{ "TEMPMap",     5, NULL,             cmd_sensor_temp_map },
	{ "TEMPNominal", 5, NULL,             cmd_sensor_temp_nominal },
	{ "TEMPOffset",  5, NULL,             cmd_sensor_temp_offset },
	{ "THERmistor",  4, NULL,             cmd_sensor_ther_nominal },
	{ 0, 0, 0, 0 }
};

struct cmd_t config_commands[] = {
	{ "DELete",    3, NULL,              cmd_delete_config },
	{ "FAN",       3, fan_c_commands,    NULL },
	{ "MBFAN",     5, mbfan_c_commands,  NULL },
	{ "Read",      1, NULL,              cmd_print_config },
	{ "SAVe",      3, NULL,              cmd_save_config },
	{ "SENSOR",    6, sensor_c_commands, NULL },
	{ 0, 0, 0, 0 }
};

struct cmd_t fan_commands[] = {
	{ "PWM",       3, NULL,              cmd_fan_pwm },
	{ "Read",      1, NULL,              cmd_fan_read },
	{ "RPM",       3, NULL,              cmd_fan_rpm },
	{ "TACho",     3, NULL,              cmd_fan_tacho },
	{ 0, 0, 0, 0 }
};

struct cmd_t mbfan_commands[] = {
	{ "PWM",       3, NULL,              cmd_mbfan_pwm },
	{ "Read",      1, NULL,              cmd_mbfan_read },
	{ "RPM",       3, NULL,              cmd_mbfan_rpm },
	{ "TACho",     3, NULL,              cmd_mbfan_tacho },
	{ 0, 0, 0, 0 }
};

struct cmd_t sensor_commands[] = {
	{ "Read",      1, NULL,              cmd_sensor_temp },
	{ "TEMP",      4, NULL,              cmd_sensor_temp },
	{ 0, 0, 0, 0 }
};

struct cmd_t measure_commands[] = {
	{ "FAN",       3, fan_commands,      cmd_fan_read },
	{ "MBFAN",     5, mbfan_commands,    cmd_mbfan_read },
	{ "Read",      1, NULL,              cmd_read },
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

	if (res < 0) {
		log_msg(LOG_INFO, "Unknown command.");
		last_error_num = -113;
	}
	else if (res == 0) {
		last_error_num = 0;
	}
	else if (res == 1) {
		last_error_num = -100;
	}
	else if (res == 2) {
		last_error_num = -102;
	} else {
		last_error_num = -1;
	}

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
		log_msg(LOG_DEBUG, "command: '%s'", cmd);
		if (cmd && strlen(cmd) > 0) {
			cmd_level = run_cmd(cmd, cmd_level, &prev_subcmd);
		}
		cmd = strtok_r(NULL, ";", &saveptr);
	}
}
