/* command.c
   Copyright (C) 2021-2024 Timo Kokkonen <tjko@iki.fi>

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
#include "pico/rand.h"
#include "hardware/watchdog.h"
#include "hardware/rtc.h"
#include "cJSON.h"
#include "fanpico.h"
#ifdef WIFI_SUPPORT
#include "lwip/ip_addr.h"
#include "lwip/stats.h"
#include "pico_telnetd/util.h"
#endif


struct cmd_t {
	const char   *cmd;
	uint8_t       min_match;
	const struct cmd_t *subcmds;
	int (*func)(const char *cmd, const char *args, int query, char *prev_cmd);
};

struct error_t {
	const char    *error;
	int            error_num;
};

/* For now, mimic some actual instrument error codes/responses... */
const struct error_t error_codes[] = {
	{ "No Error", 0 },
	{ "Command Error", -100 },
	{ "Syntax Error", -102 },
	{ "Undefined Header", -113 },
	{ NULL, 0 }
};

int last_error_num = 0;

const struct fanpico_state *st = NULL;
struct fanpico_config *conf = NULL;

/* credits.s */
extern const char fanpico_credits_text[];


/* Helper functions for commands */

typedef int (*validate_str_func_t)(const char *args);

int string_setting(const char *cmd, const char *args, int query, char *prev_cmd,
		char *var, size_t var_len, const char *name, validate_str_func_t validate_func)
{
	if (query) {
		printf("%s\n", var);
	} else {
		if (validate_func) {
			if (!validate_func(args)) {
				log_msg(LOG_WARNING, "%s invalid argument: '%s'", name, args);
				return 2;
			}
		}
		if (strcmp(var, args)) {
			log_msg(LOG_NOTICE, "%s change '%s' --> '%s'", name, var, args);
			strncopy(var, args, var_len);
		}
	}
	return 0;
}

int bitmask16_setting(const char *cmd, const char *args, int query, char *prev_cmd,
		uint16_t *mask, uint16_t len, uint8_t base, const char *name)
{
	uint32_t old = *mask;
	uint32_t new;

	if (query) {
		printf("%s\n", bitmask_to_str(old, len, base, true));
		return 0;
	}

	if (!str_to_bitmask(args, len, &new, base)) {
		if (old != new) {
			log_msg(LOG_NOTICE, "%s change 0x%lx --> 0x%lx", name, old, new);
			*mask = new;
		}
		return 0;
	}
	return 1;
}

int uint32_setting(const char *cmd, const char *args, int query, char *prev_cmd,
		uint32_t *var, uint32_t min_val, uint32_t max_val, const char *name)
{
	uint32_t val;
	int v;

	if (query) {
		printf("%lu\n", *var);
		return 0;
	}

	if (str_to_int(args, &v, 10)) {
		val = v;
		if (val >= min_val && val <= max_val) {
			if (*var != val) {
				log_msg(LOG_NOTICE, "%s change %u --> %u", name, *var, val);
				*var = val;
			}
		} else {
			log_msg(LOG_WARNING, "Invalid %s value: %s", name, args);
			return 2;
		}
		return 0;
	}
	return 1;
}

int uint8_setting(const char *cmd, const char *args, int query, char *prev_cmd,
		uint8_t *var, uint8_t min_val, uint8_t max_val, const char *name)
{
	uint8_t val;
	int v;

	if (query) {
		printf("%u\n", *var);
		return 0;
	}

	if (str_to_int(args, &v, 10)) {
		val = v;
		if (val >= min_val && val <= max_val) {
			if (*var != val) {
				log_msg(LOG_NOTICE, "%s change %u --> %u", name, *var, val);
				*var = val;
			}
		} else {
			log_msg(LOG_WARNING, "Invalid %s value: %s", name, args);
			return 2;
		}
		return 0;
	}
	return 1;
}

int bool_setting(const char *cmd, const char *args, int query, char *prev_cmd,
		bool *var, const char *name)
{
	bool val;

	if (query) {
		printf("%s\n", (*var ? "ON" : "OFF"));
		return 0;
	}

	if ((args[0] == '1' && args[1] == 0) || !strncasecmp(args, "true", 5)
		|| !strncasecmp(args, "on", 3)) {
		val = true;
	}
	else if ((args[0] == '0' && args[1] == 0) || !strncasecmp(args, "false", 6)
		|| !strncasecmp(args, "off", 4)) {
		val =  false;
	} else {
		log_msg(LOG_WARNING, "Invalid %s value: %s", name, args);
		return 2;
	}

	if (*var != val) {
		log_msg(LOG_NOTICE, "%s change %u --> %u", name, *var, val);
		*var = val;
	}
	return 0;
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
#endif


/* Command functions */

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

	printf("FanPico-%s v%s (%s; %s; SDK v%s; %s)\n\n",
		FANPICO_MODEL,
		FANPICO_VERSION,
		__DATE__,
		PICO_CMAKE_BUILD_TYPE,
		PICO_SDK_VERSION_STRING,
		PICO_BOARD);

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
		printf("%d\n", conf->led_mode);
	} else if (str_to_int(args, &mode, 10)) {
		if (mode >= 0 && mode <= 2) {
			log_msg(LOG_NOTICE, "Set system LED mode: %d -> %d", conf->led_mode, mode);
			conf->led_mode = mode;
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

int cmd_vsensors(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (!query)
		return 1;

	printf("%d\n", VSENSOR_COUNT);
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
	int level = get_log_level();
	int new_level;
	const char *name, *new_name;

	name = log_priority2str(level);

	if (query) {
		if (name) {
			printf("%s\n", name);
		} else {
			printf("%d\n", level);
		}
	} else {
		if ((new_level = str2log_priority(args)) < 0)
			return 1;
		new_name = log_priority2str(new_level);

		log_msg(LOG_NOTICE, "Change log level: %s (%d) -> %s (%d)",
			(name ? name : ""), level, new_name, new_level);
		set_log_level(new_level);
	}
	return 0;
}

int cmd_syslog_level(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int level = get_syslog_level();
	int new_level;
	const char *name, *new_name;

	name = log_priority2str(level);

	if (query) {
		if (name) {
			printf("%s\n", name);
		} else {
			printf("%d\n", level);
		}
	} else {
		if ((new_level = str2log_priority(args)) < 0)
			return 1;
		new_name = log_priority2str(new_level);
		log_msg(LOG_NOTICE, "Change syslog level: %s (%d) -> %s (%d)",
			(name ? name : "N/A"), level, new_name, new_level);
		set_syslog_level(new_level);
	}
	return 0;
}

int cmd_echo(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->local_echo, "Command Echo");
}

int cmd_display_type(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->display_type, sizeof(conf->display_type), "Display Type", NULL);
}

int cmd_display_theme(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->display_theme, sizeof(conf->display_theme), "Display Theme", NULL);
}

int cmd_display_logo(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->display_logo, sizeof(conf->display_logo), "Display Logo", NULL);
}

int cmd_display_layout_r(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->display_layout_r, sizeof(conf->display_layout_r),
			"Display Layout (Right)", NULL);
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
	update_persistent_memory();

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
	double rpm, pwm;

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
		pwm = sensor_get_duty(&conf->sensors[i].map, st->temp[i]);
		printf("sensor%d,\"%s\",%.1lf,%.1f\n", i+1,
			conf->sensors[i].name,
			st->temp[i],
			pwm);
	}

	for (i = 0; i < VSENSOR_COUNT; i++) {
		pwm = sensor_get_duty(&conf->vsensors[i].map, st->vtemp[i]);
		printf("vsensor%d,\"%s\",%.1lf,%.1f\n", i+1,
			conf->vsensors[i].name,
			st->vtemp[i],
			pwm);
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

int cmd_fan_rpm_mode(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int ret = 0;
	int fan, val;
	struct fan_output *f;
	char *tok, *saveptr, *param;

	fan = atoi(&prev_cmd[3]) - 1;
	if (fan < 0 || fan >= FAN_COUNT)
		return 1;
	f = &conf->fans[fan];

	if (query) {
		printf("%s", rpm_mode2str(f->rpm_mode));
		if (f->rpm_mode == RMODE_LRA)
			printf(",%d,%d", f->lra_low, f->lra_high);
		printf("\n");
	} else {
		param = strdup(args);
		if ((tok = strtok_r(param, ",", &saveptr)) != NULL) {
			val = str2rpm_mode(tok);
			if (val != f->rpm_mode) {
				log_msg(LOG_NOTICE, "fan%d: rpm_mode change '%s' --> '%s'",
					fan + 1, rpm_mode2str(f->rpm_mode), rpm_mode2str(val));
				f->rpm_mode = val;
			}
			if (f->rpm_mode == RMODE_LRA) {
				if ((tok = strtok_r(NULL, ",", &saveptr)) != NULL) {
					if (str_to_int(tok, &val, 10)) {
						f->lra_low = clamp_int(val, 0, 100000);
					}
					if ((tok = strtok_r(NULL, ",", &saveptr)) != NULL) {
						if (str_to_int(tok, &val, 10)) {
							f->lra_high = clamp_int(val, 0, 100000);
						}
					}
				}
			}
		} else {
			ret = 2;
		}
		free(param);
	}
	return ret;
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


int cmd_fan_tacho_hys(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	float val;

	fan = atoi(&prev_cmd[3]) - 1;
	if (fan >= 0 && fan < FAN_COUNT) {
		if (query) {
			printf("CONF:FAN%d:HYS_Tacho=%f\n", fan+1, conf->fans[fan].tacho_hyst);
		} else if (str_to_float(args, &val)) {
			if (val >= 0.0) {
				log_msg(LOG_NOTICE, "fan%d: change Hysteresis %f --> %f TAC",
					fan + 1, conf->fans[fan].tacho_hyst, val);
				conf->fans[fan].tacho_hyst = val;
			} else {
				log_msg(LOG_WARNING, "fan%d: invalid new value for Hysteresis: %f",
					fan + 1, val);
				return 2;
			}
		}
		return 0;
	}
	return 1;
}

int cmd_fan_pwm_hys(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	float val;

	//printf("in cmd_fan_pwm_hys(%s,%s,%d,%s)\n", cmd, args, query, prev_cmd);
	fan = atoi(&prev_cmd[3]) - 1;
	if (fan >= 0 && fan < FAN_COUNT) {
		if (query) {
			printf("CONF:FAN%d:HYS_Pwm=%f\n", fan+1, conf->fans[fan].pwm_hyst);
		} else if (str_to_float(args, &val)) {
			if (val >= 0.0) {
				log_msg(LOG_NOTICE, "fan%d: change Hysteresis %f --> %f PWM",
					fan + 1, conf->fans[fan].pwm_hyst, val);
				conf->fans[fan].pwm_hyst = val;
			} else {
				log_msg(LOG_WARNING, "fan%d: invalid new value for Hysteresis: %f",
					fan + 1, val);
				return 2;
			}
		}
		return 0;
	}
	return 1;
}

int cmd_hyst_supported(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;

	//printf("in cmd_hyst_supported(%s,%s,%d,%s)\n", cmd, args, query, prev_cmd);
	fan = atoi(&prev_cmd[3]) - 1;
	if (query) {
		printf("CONF:FAN%d:HYS_Pwm=%f\t", fan+1, conf->fans[fan].pwm_hyst);
		printf("CONF:FAN%d:HYS_Tacho=%f\n", fan+1, conf->fans[fan].tacho_hyst);
		return 0;
	}
	return 1;
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

int cmd_mbfan_rpm_mode(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int ret = 0;
	int fan, val;
	char *tok, *saveptr, *param;
	struct mb_input *m;

	fan = atoi(&prev_cmd[5]) - 1;
	if (fan < 0 || fan >= MBFAN_COUNT)
		return 1;
	m = &conf->mbfans[fan];


	if (query) {
		printf("%s", rpm_mode2str(m->rpm_mode));
		if (m->rpm_mode == RMODE_LRA)
			printf(",%d,%s", m->lra_treshold, (m->lra_invert ? "HIGH" : "LOW"));
		printf("\n");
	} else {
		param = strdup(args);
		if ((tok = strtok_r(param, ",", &saveptr)) != NULL) {
			val = str2rpm_mode(tok);
			if (val != m->rpm_mode) {
				log_msg(LOG_NOTICE, "mbfan%d: rpm_mode change '%s' -> '%s'",
					fan + 1, rpm_mode2str(m->rpm_mode), rpm_mode2str(val));
				m->rpm_mode = val;
			}
			if (m->rpm_mode == RMODE_LRA) {
				if ((tok = strtok_r(NULL, ",", &saveptr)) != NULL) {
					if (str_to_int(tok, &val, 10)) {
						m->lra_treshold = clamp_int(val, 0, 100000);
					}
					if ((tok = strtok_r(NULL, ",", &saveptr)) != NULL) {
						bool invert = false;
						if (!strncasecmp(tok, "HIGH", 1))
							invert = true;
						m->lra_invert = invert;
					}
				}
			}
		} else {
			ret = 2;
		}
		free(param);
	}

	return ret;
}

int cmd_mbfan_source(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	int type, val, d_o, d_n, ocount;
	char *tok, *saveptr, *param;
	uint8_t new_sources[FAN_MAX_COUNT];
	enum tacho_source_types s_type;
	int ret = 0;

	memset(new_sources, 0, sizeof(new_sources));

	fan = atoi(&prev_cmd[5]) - 1;
	if (fan < 0 || fan >= MBFAN_COUNT)
		return 1;

	s_type = conf->mbfans[fan].s_type;

	if (query) {
		printf("%s,", tacho_source2str(s_type));
		switch (s_type) {

		case TACHO_FIXED:
		case TACHO_FAN:
			val = conf->mbfans[fan].s_id;
			if (s_type != TACHO_FIXED)
				val++;
			printf("%u", val);
			break;

		case TACHO_MIN:
		case TACHO_MAX:
		case TACHO_AVG:
			ocount = 0;
			for (int i = 0; i < FAN_COUNT; i++) {
				if (conf->mbfans[fan].sources[i]) {
					if (ocount > 0)
						printf(",");
					printf("%u", i + 1);
					ocount++;
				}
			}
			break;
		}
		printf("\n");
	} else {
		param = strdup(args);
		if ((tok = strtok_r(param, ",", &saveptr)) != NULL) {
			type = str2tacho_source(tok);
			d_n = (type != TACHO_FIXED ? 1 : 0);
			while ((tok = strtok_r(NULL, ",", &saveptr)) != NULL) {
				val = atoi(tok) - d_n;
				if (valid_tacho_source_ref(type, val)) {
					if (type == TACHO_FIXED || type == TACHO_FAN) {
						d_o = (conf->mbfans[fan].s_type != TACHO_FIXED ? 1 : 0);
						log_msg(LOG_NOTICE, "mbfan%d: change source %s,%u --> %s,%u",
							fan + 1,
							tacho_source2str(conf->mbfans[fan].s_type),
							conf->mbfans[fan].s_id + d_o,
							tacho_source2str(type),
							val + d_n);
						conf->mbfans[fan].s_type = type;
						conf->mbfans[fan].s_id = val;
						break;
					}
					new_sources[val] = 1;
				} else {
					log_msg(LOG_WARNING, "mbfan%d: invalid source: %s",
						fan + 1, args);
					ret = 2;
					break;
				}
			}

			if (type == TACHO_MIN || type == TACHO_MAX || type == TACHO_AVG) {
				int scount = 0;
				for (int i = 0; i < FAN_COUNT; i++) {
					if (new_sources[i])
						scount++;
				}
				if (scount >= 2) {
					log_msg(LOG_NOTICE, "mbfan%d: new source %s", fan + 1, args);
					conf->mbfans[fan].s_type = type;
					conf->mbfans[fan].s_id = 0;
					memcpy(conf->mbfans[fan].sources, new_sources,
						sizeof(conf->mbfans[fan].sources));
				} else {
					log_msg(LOG_WARNING, "mbfan%d: too few parameters: %s",
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

int  cmd_sensor_adc_vref(const char *cmd, const char *args, int query, char *prev_cmd)
{
	float val;

	if (query) {
		printf("%.4f\n", conf->adc_vref);
		return 0;
	} else if (str_to_float(args, &val)) {
		if (val > 0.0) {
			log_msg(LOG_NOTICE, "Change ADC voltage reference from %.4f volts --> %.4f volts",
				conf->adc_vref,	val);
			conf->adc_vref = val;
			return 0;
		} else {
		    log_msg(LOG_WARNING, "Invalid ADC voltage reference: %f", val);
			return 2;
		}
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

int cmd_vsensor_name(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor;

	sensor = atoi(&prev_cmd[7]) - 1;
	if (sensor >= 0 && sensor < VSENSOR_COUNT) {
		if (query) {
			printf("%s\n", conf->vsensors[sensor].name);
		} else {
			log_msg(LOG_NOTICE, "vsensor%d: change name '%s' --> '%s'", sensor + 1,
				conf->vsensors[sensor].name, args);
			strncopy(conf->vsensors[sensor].name, args,
				sizeof(conf->vsensors[sensor].name));
		}
		return 0;
	}
	return 1;
}

int cmd_vsensor_source(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor, val, i;
	uint8_t vsmode, selected[VSENSOR_SOURCE_MAX_COUNT];
	float default_temp;
	int timeout;
	char *tok, *saveptr, *param, temp_str[32], tmp[8];
	int ret = 0;
	int count = 0;

	sensor = atoi(&prev_cmd[7]) - 1;
	if (sensor < 0 || sensor >= VSENSOR_COUNT)
		return 1;
	vsmode = conf->vsensors[sensor].mode;

	if (query) {
		printf("%s", vsmode2str(vsmode));
		if (vsmode == VSMODE_MANUAL) {
			printf(",%0.2f,%ld",
				conf->vsensors[sensor].default_temp,
				conf->vsensors[sensor].timeout);
		} else if (vsmode == VSMODE_ONEWIRE) {
			printf(",%016llx", conf->vsensors[sensor].onewire_addr);
		} else {
			for(i = 0; i < VSENSOR_SOURCE_MAX_COUNT; i++) {
				if (conf->vsensors[sensor].sensors[i]) {
					printf(",%d", conf->vsensors[sensor].sensors[i]);
				}
			}
		}
		printf("\n");
	} else {
		ret = 2;
		if ((param = strdup(args)) == NULL)
			return 1;

		if ((tok = strtok_r(param, ",", &saveptr)) != NULL) {
			vsmode = str2vsmode(tok);
			if (vsmode == VSMODE_MANUAL) {
				tok = strtok_r(NULL, ",", &saveptr);
				if (str_to_float(tok, &default_temp)) {
					tok = strtok_r(NULL, ",", &saveptr);
					if (str_to_int(tok, &timeout, 10)) {
						if (timeout < 0)
							timeout = 0;
						log_msg(LOG_NOTICE, "vsensor%d: set source to %s,%0.2f,%d",
							sensor + 1,
							vsmode2str(vsmode),
							default_temp,
							timeout);
						conf->vsensors[sensor].mode = vsmode;
						conf->vsensors[sensor].default_temp = default_temp;
						conf->vsensors[sensor].timeout = timeout;
						ret = 0;
					}
				}
			} else if (vsmode == VSMODE_ONEWIRE) {
				tok = strtok_r(NULL, ",", &saveptr);
				uint64_t addr = strtoull(tok, NULL, 16);
				if (addr > 0) {
					log_msg(LOG_NOTICE, "vsensor%d: set source to %s,%016llx",
						sensor + 1,
						vsmode2str(vsmode),
						addr);
					conf->vsensors[sensor].mode = vsmode;
					conf->vsensors[sensor].onewire_addr = addr;
				}
			} else {
				temp_str[0] = 0;
				for(i = 0; i < VSENSOR_SOURCE_MAX_COUNT; i++)
					selected[i] = 0;
				while((tok = strtok_r(NULL, ",", &saveptr)) != NULL) {
					if (count < VSENSOR_SOURCE_MAX_COUNT && str_to_int(tok, &val, 10)) {
						if ((val >= 1 && val <= SENSOR_COUNT)
							|| (val >= 101 && val <= 100 + VSENSOR_COUNT)) {
							selected[count++] = val;
							snprintf(tmp, sizeof(tmp), ",%d", val);
							strncatenate(temp_str, tmp, sizeof(temp_str));
						}
					}
				}
				if (count >= 2) {
					log_msg(LOG_NOTICE, "vsensor%d: set source to %s%s",
						sensor + 1,
						vsmode2str(vsmode),
						temp_str);
					conf->vsensors[sensor].mode = vsmode;
					for(i = 0; i < SENSOR_COUNT; i++) {
						conf->vsensors[sensor].sensors[i] = selected[i];
					}
					ret = 0;
				}
			}
		}
		free(param);
	}

	return ret;
}

int cmd_vsensor_temp_map(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor, i, count;
	float val;
	char *arg, *t, *saveptr;
	struct temp_map *map;
	struct temp_map new_map;
	int ret = 0;

	sensor = atoi(&prev_cmd[7]) - 1;
	if (sensor < 0 || sensor >= VSENSOR_COUNT)
		return 1;
	map = &conf->vsensors[sensor].map;
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
			log_msg(LOG_WARNING, "vsensor%d: invalid new map: %s", sensor + 1, args);
			ret = 2;
		}
		free(arg);
	}

	return ret;
}

int cmd_vsensor_temp(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor;
	float d;

	if (!query)
		return 1;

	if (!strncasecmp(prev_cmd, "vsensor", 7)) {
		sensor = atoi(&prev_cmd[7]) - 1;
	} else {
		sensor = atoi(&cmd[7]) - 1;
	}

	if (sensor >= 0 && sensor < VSENSOR_COUNT) {
		d = st->vtemp[sensor];
		log_msg(LOG_DEBUG, "vsensor%d temperature = %fC", sensor + 1, d);
		printf("%.0f\n", d);
		return 0;
	}

	return 1;
}

int cmd_vsensor_filter(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor;
	int ret = 0;
	char *tok, *saveptr, *param;
	struct vsensor_input *s;
	enum signal_filter_types new_filter;
	void *new_ctx;

	sensor = atoi(&prev_cmd[7]) - 1;
	if (sensor < 0 || sensor >= VSENSOR_COUNT)
		return 1;

	s = &conf->vsensors[sensor];
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

int cmd_vsensor_write(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor;
	float val;

	if (query)
		return 1;

	if (!strncasecmp(prev_cmd, "vsensor", 7)) {
		sensor = atoi(&prev_cmd[7]) - 1;
	} else {
		sensor = atoi(&cmd[7]) - 1;
	}

	if (sensor >= 0 && sensor < VSENSOR_COUNT) {
		if (conf->vsensors[sensor].mode == VSMODE_MANUAL) {
			if (str_to_float(args, &val)) {
				log_msg(LOG_INFO, "vsensor%d: write temperature = %fC", sensor + 1, val);
				conf->vtemp[sensor] = val;
				conf->vtemp_updated[sensor] = get_absolute_time();
				return 0;
			}
		} else {
			return 2;
		}
	}

	return 1;
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
int cmd_wifi_ip(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return ip_change(cmd, args, query, prev_cmd, "IP", &conf->ip);
}

int cmd_wifi_netmask(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return ip_change(cmd, args, query, prev_cmd, "Netmask", &conf->netmask);
}

int cmd_wifi_gateway(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return ip_change(cmd, args, query, prev_cmd, "Default Gateway", &conf->gateway);
}

int cmd_wifi_syslog(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return ip_change(cmd, args, query, prev_cmd, "Syslog Server", &conf->syslog_server);
}

int cmd_wifi_ntp(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return ip_change(cmd, args, query, prev_cmd, "NTP Server", &conf->ntp_server);
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
	return string_setting(cmd, args, query, prev_cmd,
			conf->wifi_ssid, sizeof(conf->wifi_ssid), "WiFi SSID", NULL);
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
	return string_setting(cmd, args, query, prev_cmd,
			conf->wifi_country, sizeof(conf->wifi_country),
			"WiFi Country", valid_wifi_country);
}

int cmd_wifi_password(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->wifi_passwd, sizeof(conf->wifi_passwd), "WiFi Password", NULL);
}

int cmd_wifi_hostname(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->hostname, sizeof(conf->hostname),
			"WiFi Hostname", valid_hostname);
}

int cmd_wifi_mode(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return uint8_setting(cmd, args, query, prev_cmd,
			&conf->wifi_mode, 0, 1, "WiFi Mode");
}

int cmd_mqtt_server(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_server, sizeof(conf->mqtt_server), "MQTT Server", NULL);
}

int cmd_mqtt_port(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return uint32_setting(cmd, args, query, prev_cmd,
			&conf->mqtt_port, 0, 65535, "MQTT Port");
}

int cmd_mqtt_user(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_user, sizeof(conf->mqtt_user), "MQTT User", NULL);
}

int cmd_mqtt_pass(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_pass, sizeof(conf->mqtt_pass), "MQTT Password", NULL);
}

int cmd_mqtt_status_interval(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return uint32_setting(cmd, args, query, prev_cmd,
			&conf->mqtt_status_interval, 0, (86400 * 30), "MQTT Publish Status Interval");
}

int cmd_mqtt_temp_interval(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return uint32_setting(cmd, args, query, prev_cmd,
			&conf->mqtt_temp_interval, 0, (86400 * 30), "MQTT Publish Temp Interval");
}

int cmd_mqtt_rpm_interval(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return uint32_setting(cmd, args, query, prev_cmd,
			&conf->mqtt_rpm_interval, 0, (86400 * 30), "MQTT Publish RPM Interval");
}

int cmd_mqtt_duty_interval(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return uint32_setting(cmd, args, query, prev_cmd,
			&conf->mqtt_duty_interval, 0, (86400 * 30), "MQTT Publish PWM Interval");
}

int cmd_mqtt_allow_scpi(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->mqtt_allow_scpi, "MQTT Allow SCPI Commands");
}

int cmd_mqtt_status_topic(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_status_topic, sizeof(conf->mqtt_status_topic),
			"MQTT Status Topic", NULL);
}

int cmd_mqtt_cmd_topic(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_cmd_topic, sizeof(conf->mqtt_cmd_topic),
			"MQTT Command Topic", NULL);
}

int cmd_mqtt_resp_topic(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_resp_topic,
			sizeof(conf->mqtt_resp_topic), "MQTT Response Topic", NULL);
}

int cmd_mqtt_temp_topic(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_temp_topic,
			sizeof(conf->mqtt_temp_topic), "MQTT Temperature Topic", NULL);
}

int cmd_mqtt_fan_rpm_topic(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_fan_rpm_topic,
			sizeof(conf->mqtt_fan_rpm_topic), "MQTT Fan RPM Topic", NULL);
}

int cmd_mqtt_fan_duty_topic(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_fan_duty_topic,
			sizeof(conf->mqtt_fan_duty_topic), "MQTT Fan PWM Topic", NULL);
}

int cmd_mqtt_mbfan_rpm_topic(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_mbfan_rpm_topic,
			sizeof(conf->mqtt_mbfan_rpm_topic), "MQTT MBFan RPM Topic", NULL);
}

int cmd_mqtt_mbfan_duty_topic(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_mbfan_duty_topic,
			sizeof(conf->mqtt_mbfan_duty_topic), "MQTT MBFan PWM Topic", NULL);
}

int cmd_mqtt_mask_temp(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return bitmask16_setting(cmd, args, query, prev_cmd,
				&conf->mqtt_temp_mask, SENSOR_MAX_COUNT,
				1, "MQTT Temperature Mask");
}

int cmd_mqtt_mask_fan_rpm(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return bitmask16_setting(cmd, args, query, prev_cmd,
				&conf->mqtt_fan_rpm_mask, FAN_MAX_COUNT,
				1, "MQTT Fan RPM Mask");
}

int cmd_mqtt_mask_fan_duty(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return bitmask16_setting(cmd, args, query, prev_cmd,
				&conf->mqtt_fan_duty_mask, FAN_MAX_COUNT,
				1, "MQTT Fan PWM Mask");
}

int cmd_mqtt_mask_mbfan_rpm(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return bitmask16_setting(cmd, args, query, prev_cmd,
				&conf->mqtt_mbfan_rpm_mask, MBFAN_MAX_COUNT,
				1, "MQTT MBFan RPM Mask");
}

int cmd_mqtt_mask_mbfan_duty(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return bitmask16_setting(cmd, args, query, prev_cmd,
				&conf->mqtt_mbfan_duty_mask, MBFAN_MAX_COUNT,
				1, "MQTT MBFan PWM Mask");
}

#if TLS_SUPPORT
int cmd_mqtt_tls(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->mqtt_tls, "MQTT TLS Mode");
}

int cmd_tls_pkey(const char *cmd, const char *args, int query, char *prev_cmd)
{
	char *buf;
	uint32_t buf_len = 4096;
	uint32_t file_size;
	int incount = 1;
	int res = 0;

	if (query) {
		res = flash_read_file(&buf, &file_size, "key.pem");
		if (res == 0 && buf != NULL) {
			printf("%s\n", buf);
			free(buf);
			return 0;
		} else {
			printf("No private key present.\n");
		}
		return 2;
	}

	if ((buf = malloc(buf_len)) == NULL) {
		log_msg(LOG_ERR,"cmd_tls_pkey(): not enough memory");
		return 2;
	}
	buf[0] = 0;

#if WATCHDOG_ENABLED
	watchdog_disable();
#endif
	if (!strncasecmp(args, "DELETE", 7)) {
		res = flash_delete_file("key.pem");
		if (res == -2) {
			printf("No private key present.\n");
			res = 0;
		}
		else if (res) {
			printf("Failed to delete private key: %d\n", res);
			res = 2;
		} else {
			printf("Private key successfully deleted.\n");
		}
	}
	else {
		int v;
		if (str_to_int(args, &v, 10)) {
			if (v >= 1 && v <= 3)
				incount = v;
		}
		printf("Paste private key in PEM format:\n");
		for(int i = 0; i < incount; i++) {
			if (read_pem_file(buf, buf_len, 5000, true) != 1) {
				printf("Invalid private key!\n");
				res = 2;
				break;
			}
		}
		if (res == 0) {
			res = flash_write_file(buf, strlen(buf) + 1, "key.pem");
			if (res) {
				printf("Failed to save private key.\n");
				res = 2;
			} else {
				printf("Private key successfully saved. (length=%u)\n",
					strlen(buf));
				res = 0;
			}
		}
	}
#if WATCHDOG_ENABLED
	watchdog_enable(WATCHDOG_REBOOT_DELAY, 1);
#endif

	free(buf);
	return res;
}

int cmd_tls_cert(const char *cmd, const char *args, int query, char *prev_cmd)
{
	char *buf;
	uint32_t buf_len = 8192;
	uint32_t file_size;
	int res = 0;

	if (query) {
		res = flash_read_file(&buf, &file_size, "cert.pem");
		if (res == 0 && buf != NULL) {
			printf("%s\n", buf);
			free(buf);
			return 0;
		} else {
			printf("No certificate present.\n");
		}
		return 2;
	}

	if ((buf = malloc(buf_len)) == NULL) {
		log_msg(LOG_ERR,"cmd_tls_cert(): not enough memory");
		return 2;
	}
	buf[0] = 0;

#if WATCHDOG_ENABLED
	watchdog_disable();
#endif
	if (!strncasecmp(args, "DELETE", 7)) {
		res = flash_delete_file("cert.pem");
		if (res == -2) {
			printf("No certificate present.\n");
			res = 0;
		}
		else if (res) {
			printf("Failed to delete certificate: %d\n", res);
			res = 2;
		} else {
			printf("Certificate successfully deleted.\n");
		}
	}
	else {
		printf("Paste certificate in PEM format:\n");

		if (read_pem_file(buf, buf_len, 5000, false) != 1) {
			printf("Invalid private key!\n");
			res = 2;
		} else {
			res = flash_write_file(buf, strlen(buf) + 1, "cert.pem");
			if (res) {
				printf("Failed to save certificate.\n");
				res = 2;
			} else {
				printf("Certificate successfully saved. (length=%u)\n",
					strlen(buf));
				res = 0;
			}
		}
	}
#if WATCHDOG_ENABLED
	watchdog_enable(WATCHDOG_REBOOT_DELAY, 1);
#endif

	free(buf);
	return res;
}
#endif /* TLS_SUPPORT */

int cmd_telnet_auth(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->telnet_auth, "Telnet Server Authentication");
}

int cmd_telnet_rawmode(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->telnet_raw_mode, "Telnet Server Raw Mode");
}

int cmd_telnet_server(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->telnet_active, "Telnet Server");
}

int cmd_telnet_port(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return uint32_setting(cmd, args, query, prev_cmd,
			&conf->telnet_port, 0, 65535, "Telnet Port");
}

int cmd_telnet_user(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->telnet_user, sizeof(conf->telnet_user),
			"Telnet Username", NULL);
}

int cmd_telnet_pass(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query) {
		printf("%s\n", cfg->telnet_pwhash);
		return 0;
	}

	if (strlen(args) > 0) {
		strncopy(conf->telnet_pwhash, generate_sha512crypt_pwhash(args),
			sizeof(conf->telnet_pwhash));
	} else {
		conf->telnet_pwhash[0] = 0;
		log_msg(LOG_NOTICE, "Telnet password removed.");
	}
	return 0;
}
#endif /* WIFI_SUPPOERT */

int cmd_time(const char *cmd, const char *args, int query, char *prev_cmd)
{
	datetime_t t;

	if (query) {
		if (rtc_get_datetime(&t)) {
			printf("%04d-%02d-%02d %02d:%02d:%02d\n",
				t.year, t.month, t.day,	t.hour, t.min, t.sec);
		}
		return 0;
	}

	if (str_to_datetime(args, &t)) {
		if (rtc_set_datetime(&t)) {
			log_msg(LOG_NOTICE, "Set system clock: %04d-%02d-%02d %02d:%02d:%02d",
				t.year, t.month, t.day, t.hour, t.min, t.sec);
			return 0;
		}
	}
	return 2;
}

int cmd_timezone(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->timezone, sizeof(conf->timezone), "Timezone", NULL);
}

int cmd_uptime(const char *cmd, const char *args, int query, char *prev_cmd)
{
	uint32_t secs = to_us_since_boot(get_absolute_time()) / 1000000;
	uint32_t mins =  secs / 60;
	uint32_t hours = mins / 60;
	uint32_t days = hours / 24;

	if (!query)
		return 1;

	printf("up %lu days, %lu hours, %lu minutes%s\n", days, hours % 24, mins % 60,
		(rebooted_by_watchdog ? " [rebooted by watchdog]" : ""));

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
	return string_setting(cmd, args, query, prev_cmd,
			conf->name, sizeof(conf->name), "System Name", NULL);
}


int cmd_lfs(const char *cmd, const char *args, int query, char *prev_cmd)
{
	size_t size, free, used, files, dirs;

	if (!query)
		return 1;
	if (flash_get_fs_info(&size, &free, &files, &dirs, NULL) < 0)
		return 2;

	used = size - free;
	printf("Filesystem size:                       %u\n", size);
	printf("Filesystem used:                       %u\n", used);
	printf("Filesystem free:                       %u\n", free);
	printf("Number of files:                       %u\n", files);
	printf("Number of subdirectories:              %u\n", dirs);

	return 0;
}

int cmd_lfs_format(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (query)
		return 1;

	printf("Formatting flash filesystem...\n");
	if (flash_format(true))
		return 2;
	printf("Filesystem successfully formatted.\n");

	return 0;
}

int cmd_flash(const char *cmd, const char *args, int query, char *prev_cmd)
{
	if (!query)
		return 1;

	print_rp2040_flashinfo();
	return 0;
}


#define TEST_MEM_SIZE (264*1024)

int cmd_memory(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int blocksize;

	if (query) {
		print_rp2040_meminfo();
		printf("mallinfo:\n");
		print_mallinfo();
		return 0;
	}

	if (str_to_int(args, &blocksize, 10)) {
		if (blocksize < 512)
			blocksize = 512;
		if (blocksize > 8192)
			blocksize= 8192;
	} else {
		blocksize = 1024;
	}

	/* Test for largest available memory block... */
	void *buf = NULL;
	size_t bufsize = blocksize;
	do {
		if (buf) {
			free(buf);
			bufsize += blocksize;
		}
		buf = malloc(bufsize);
	} while (buf && bufsize < TEST_MEM_SIZE);
	printf("Largest available memory block:        %u bytes\n",
		bufsize - blocksize);

	/* Test how much memory available in 'blocksize' blocks... */
	int i = 0;
	int max = TEST_MEM_SIZE / blocksize + 1;
	void **refbuf = malloc(max * sizeof(void*));
	if (refbuf) {
		memset(refbuf, 0, max * sizeof(void*));
		while (i < max) {
			if (!(refbuf[i] = malloc(blocksize)))
				break;
			i++;
		}
	}
	printf("Total available memory:                %u bytes (%d x %dbytes)\n",
		i * blocksize, i, blocksize);
	if (refbuf) {
		i = 0;
		while (i < max && refbuf[i]) {
			free(refbuf[i++]);
		}
		free(refbuf);
	}
	return 0;
}

int cmd_onewire(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->onewire_active, "1-Wire Bus status");
}

int cmd_onewire_sensors(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int i;

	if (!query)
		return 1;

	if (!conf->onewire_active)
		return -1;

	for (i = 0; i < ONEWIRE_MAX_COUNT; i++) {
		uint64_t addr = onewire_address(i);
		if (addr)
			printf("%d,%016llx,%1.1f\n", i + 1,
				addr, st->onewire_temp[i]);
	}

	return 0;
}

int cmd_serial(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->serial_active, "Serial Console status");
}

int cmd_spi(const char *cmd, const char *args, int query, char *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->spi_active, "SPI (LCD Display) status");
}


const struct cmd_t display_commands[] = {
	{ "LAYOUTR",   7, NULL,              cmd_display_layout_r },
	{ "LOGO",      4, NULL,              cmd_display_logo },
	{ "THEMe",     4, NULL,              cmd_display_theme },
	{ 0, 0, 0, 0 }
};

const struct cmd_t lfs_commands[] = {
	{ "FORMAT",    6, NULL,              cmd_lfs_format },
	{ 0, 0, 0, 0 }
};

const struct cmd_t wifi_commands[] = {
#ifdef WIFI_SUPPORT
	{ "COUntry",   3, NULL,              cmd_wifi_country },
	{ "GATEway",   4, NULL,              cmd_wifi_gateway },
	{ "HOSTname",  4, NULL,              cmd_wifi_hostname },
	{ "IPaddress", 2, NULL,              cmd_wifi_ip },
	{ "MAC",       3, NULL,              cmd_wifi_mac },
	{ "NETMask",   4, NULL,              cmd_wifi_netmask },
	{ "NTP",       3, NULL,              cmd_wifi_ntp },
	{ "MODE",      4, NULL,              cmd_wifi_mode },
	{ "PASSword",  4, NULL,              cmd_wifi_password },
	{ "SSID",      4, NULL,              cmd_wifi_ssid },
	{ "STATS",     5, NULL,              cmd_wifi_stats },
	{ "STATus",    4, NULL,              cmd_wifi_status },
	{ "SYSLOG",    6, NULL,              cmd_wifi_syslog },
#endif
	{ 0, 0, 0, 0 }
};

#ifdef WIFI_SUPPORT
const struct cmd_t mqtt_mask_commands[] = {
	{ "TEMP",      4, NULL,              cmd_mqtt_mask_temp },
	{ "FANRPM",    6, NULL,              cmd_mqtt_mask_fan_rpm },
	{ "FANPWM",    6, NULL,              cmd_mqtt_mask_fan_duty },
	{ "MBFANRPM",  8, NULL,              cmd_mqtt_mask_mbfan_rpm },
	{ "MBFANPWM",  8, NULL,              cmd_mqtt_mask_mbfan_duty },
	{ 0, 0, 0, 0 }
};

const struct cmd_t mqtt_interval_commands[] = {
	{ "STATUS",    6, NULL,              cmd_mqtt_status_interval },
	{ "TEMP",      4, NULL,              cmd_mqtt_temp_interval },
	{ "RPM",       3, NULL,              cmd_mqtt_rpm_interval },
	{ "PWM",       3, NULL,              cmd_mqtt_duty_interval },
	{ 0, 0, 0, 0 }
};

const struct cmd_t mqtt_topic_commands[] = {
	{ "STATus",    4, NULL,              cmd_mqtt_status_topic },
	{ "COMMand",   4, NULL,              cmd_mqtt_cmd_topic },
	{ "RESPonse",  4, NULL,              cmd_mqtt_resp_topic },
	{ "TEMP",      4, NULL,              cmd_mqtt_temp_topic },
	{ "FANRPM",    6, NULL,              cmd_mqtt_fan_rpm_topic },
	{ "FANPWM",    6, NULL,              cmd_mqtt_fan_duty_topic },
	{ "MBFANRPM",  8, NULL,              cmd_mqtt_mbfan_rpm_topic },
	{ "MBFANPWM",  8, NULL,              cmd_mqtt_mbfan_duty_topic },
	{ 0, 0, 0, 0 }
};

const struct cmd_t mqtt_commands[] = {
	{ "SERVer",    4, NULL,              cmd_mqtt_server },
	{ "PORT",      4, NULL,              cmd_mqtt_port },
	{ "USER",      4, NULL,              cmd_mqtt_user },
	{ "PASSword",  4, NULL,              cmd_mqtt_pass },
	{ "SCPI",      4, NULL,              cmd_mqtt_allow_scpi },
#if TLS_SUPPORT
	{ "TLS",       3, NULL,              cmd_mqtt_tls },
#endif
	{ "INTerval",  3, mqtt_interval_commands, NULL },
	{ "MASK",      4, mqtt_mask_commands, NULL },
	{ "TOPIC",     5, mqtt_topic_commands, NULL },
	{ 0, 0, 0, 0 }
};
#endif

const struct cmd_t tls_commands[] = {
#ifdef WIFI_SUPPORT
#if TLS_SUPPORT
	{ "CERT",      4, NULL,              cmd_tls_cert },
	{ "PKEY",      4, NULL,              cmd_tls_pkey },
#endif
#endif
	{ 0, 0, 0, 0 }
};

const struct cmd_t telnet_commands[] = {
#ifdef WIFI_SUPPORT
	{ "AUTH",      4, NULL,              cmd_telnet_auth },
	{ "PORT",      4, NULL,              cmd_telnet_port },
	{ "RAWmode",   3, NULL,              cmd_telnet_rawmode },
	{ "SERVer",    4, NULL,              cmd_telnet_server },
	{ "PASSword",  4, NULL,              cmd_telnet_pass },
	{ "USER",      4, NULL,              cmd_telnet_user },
#endif
	{ 0, 0, 0, 0 }
};

const struct cmd_t onewire_commands[] = {
	{ "SENsors",   3, NULL,              cmd_onewire_sensors },
	{ 0, 0, 0, 0 }
};

const struct cmd_t system_commands[] = {
	{ "DEBUG",     5, NULL,              cmd_debug }, /* Obsolete ? */
	{ "DISPlay",   4, display_commands,  cmd_display_type },
	{ "ECHO",      4, NULL,              cmd_echo },
	{ "ERRor",     3, NULL,              cmd_err },
	{ "FANS",      4, NULL,              cmd_fans },
	{ "FLASH",     5, NULL,              cmd_flash },
	{ "LED",       3, NULL,              cmd_led },
	{ "LFS",       3, lfs_commands,      cmd_lfs },
	{ "LOG",       3, NULL,              cmd_log_level },
	{ "MBFANS",    6, NULL,              cmd_mbfans },
	{ "MEMory",    3, NULL,              cmd_memory },
#ifdef WIFI_SUPPORT
	{ "MQTT",      4, mqtt_commands,     NULL },
#endif
	{ "NAME",      4, NULL,              cmd_name },
#if ONEWIRE_SUPPORT
	{ "ONEWIRE",   7, onewire_commands,  cmd_onewire },
#endif
	{ "SENSORS",   7, NULL,              cmd_sensors },
	{ "SERIAL",    6, NULL,              cmd_serial },
	{ "SPI",       3, NULL,              cmd_spi },
	{ "SYSLOG",    6, NULL,              cmd_syslog_level },
	{ "TELNET",    6, telnet_commands,   NULL },
	{ "TIMEZONE",  8, NULL,              cmd_timezone },
	{ "TIME",      4, NULL,              cmd_time },
	{ "TLS",       3, tls_commands,      NULL },
	{ "UPGRADE",   7, NULL,              cmd_usb_boot },
	{ "UPTIme",    4, NULL,              cmd_uptime },
	{ "VERsion",   3, NULL,              cmd_version },
	{ "VENSORS",   8, NULL,              cmd_vsensors },
	{ "VREFadc",   4, NULL,              cmd_sensor_adc_vref },
	{ "WIFI",      4, wifi_commands,     cmd_wifi },
	{ 0, 0, 0, 0 }
};

const struct cmd_t fan_hyst_commands[] = {
	{ "TACho",     3, NULL,              cmd_fan_tacho_hys },
	{ "PWM",       3, NULL,              cmd_fan_pwm_hys   },
	{ 0, 0, 0, 0 }
};

const struct cmd_t fan_c_commands[] = {
	{ "FILTER",    6, NULL,              cmd_fan_filter },
	{ "MAXpwm",    3, NULL,              cmd_fan_max_pwm },
	{ "MINpwm",    3, NULL,              cmd_fan_min_pwm },
	{ "NAME",      4, NULL,              cmd_fan_name },
	{ "PWMCoeff",  4, NULL,              cmd_fan_pwm_coef },
	{ "PWMMap",    4, NULL,              cmd_fan_pwm_map },
	{ "RPMFactor", 4, NULL,              cmd_fan_rpm_factor },
	{ "RPMMOde",   5, NULL,              cmd_fan_rpm_mode },
	{ "SOUrce",    3, NULL,              cmd_fan_source },
	{ "HYSTeresis",4, NULL,              cmd_hyst_supported },
	{ "HYS_Tacho", 5, NULL,              cmd_fan_tacho_hys  },
	{ "HYS_Pwm",   5, NULL,              cmd_fan_pwm_hys    },
	{ 0, 0, 0, 0 }
};

const struct cmd_t mbfan_c_commands[] = {
	{ "FILTER",    6, NULL,              cmd_mbfan_filter },
	{ "MAXrpm",    3, NULL,              cmd_mbfan_max_rpm },
	{ "MINrpm",    3, NULL,              cmd_mbfan_min_rpm },
	{ "NAME",      4, NULL,              cmd_mbfan_name },
	{ "RPMCoeff",  4, NULL,              cmd_mbfan_rpm_coef },
	{ "RPMFactor", 4, NULL,              cmd_mbfan_rpm_factor },
	{ "RPMMOde",   5, NULL,              cmd_mbfan_rpm_mode },
	{ "RPMMap",    4, NULL,              cmd_mbfan_rpm_map },
	{ "SOUrce",    3, NULL,              cmd_mbfan_source },
	{ 0, 0, 0, 0 }
};

const struct cmd_t sensor_c_commands[] = {
	{ "BETAcoeff",   4, NULL,            cmd_sensor_beta_coef },
	{ "FILTER",      6, NULL,            cmd_sensor_filter },
	{ "NAME",        4, NULL,            cmd_sensor_name },
	{ "TEMPCoeff",   5, NULL,            cmd_sensor_temp_coef },
	{ "TEMPMap",     5, NULL,            cmd_sensor_temp_map },
	{ "TEMPNominal", 5, NULL,            cmd_sensor_temp_nominal },
	{ "TEMPOffset",  5, NULL,            cmd_sensor_temp_offset },
	{ "THERmistor",  4, NULL,            cmd_sensor_ther_nominal },
	{ 0, 0, 0, 0 }
};

const struct cmd_t vsensor_c_commands[] = {
	{ "FILTER",      6, NULL,            cmd_vsensor_filter },
	{ "NAME",        4, NULL,            cmd_vsensor_name },
	{ "SOUrce",      3, NULL,            cmd_vsensor_source },
	{ "TEMPMap",     5, NULL,            cmd_vsensor_temp_map },
	{ 0, 0, 0, 0 }
};

const struct cmd_t config_commands[] = {
	{ "DELete",    3, NULL,              cmd_delete_config },
	{ "FAN",       3, fan_c_commands,    NULL },
	{ "MBFAN",     5, mbfan_c_commands,  NULL },
	{ "Read",      1, NULL,              cmd_print_config },
	{ "SAVe",      3, NULL,              cmd_save_config },
	{ "SENSOR",    6, sensor_c_commands, NULL },
	{ "VSENSOR",   7, vsensor_c_commands, NULL },
	{ 0, 0, 0, 0 }
};

const struct cmd_t fan_commands[] = {
	{ "PWM",       3, NULL,              cmd_fan_pwm },
	{ "Read",      1, NULL,              cmd_fan_read },
	{ "RPM",       3, NULL,              cmd_fan_rpm },
	{ "TACho",     3, NULL,              cmd_fan_tacho },
	{ 0, 0, 0, 0 }
};

const struct cmd_t mbfan_commands[] = {
	{ "PWM",       3, NULL,              cmd_mbfan_pwm },
	{ "Read",      1, NULL,              cmd_mbfan_read },
	{ "RPM",       3, NULL,              cmd_mbfan_rpm },
	{ "TACho",     3, NULL,              cmd_mbfan_tacho },
	{ 0, 0, 0, 0 }
};

const struct cmd_t sensor_commands[] = {
	{ "Read",      1, NULL,              cmd_sensor_temp },
	{ "TEMP",      4, NULL,              cmd_sensor_temp },
	{ 0, 0, 0, 0 }
};

const struct cmd_t vsensor_commands[] = {
	{ "Read",      1, NULL,              cmd_vsensor_temp },
	{ "TEMP",      4, NULL,              cmd_vsensor_temp },
	{ 0, 0, 0, 0 }
};

const struct cmd_t measure_commands[] = {
	{ "FAN",       3, fan_commands,      cmd_fan_read },
	{ "MBFAN",     5, mbfan_commands,    cmd_mbfan_read },
	{ "Read",      1, NULL,              cmd_read },
	{ "SENSOR",    6, sensor_commands,   cmd_sensor_temp },
	{ "VSENSOR",   7, vsensor_commands,  cmd_vsensor_temp },
	{ 0, 0, 0, 0 }
};

const struct cmd_t write_commands[] = {
	{ "VSENSOR",   7, NULL,              cmd_vsensor_write },
	{ 0, 0, 0, 0 }
};

const struct cmd_t commands[] = {
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
	{ "WRIte",     3, write_commands,    NULL },
	{ 0, 0, 0, 0 }
};



const struct cmd_t* run_cmd(char *cmd, const struct cmd_t *cmd_level, char **prev_subcmd)
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
						if (!query)
							mutex_enter_blocking(config_mutex);
						res = cmd_level[i].func(s,
								(total_len > cmd_len+1 ? arg : ""),
								query,
								(*prev_subcmd ? *prev_subcmd : ""));
						if (!query)
							mutex_exit(config_mutex);
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


void process_command(const struct fanpico_state *state, struct fanpico_config *config, char *command)
{
	char *saveptr, *cmd;
	char *prev_subcmd = NULL;
	const struct cmd_t *cmd_level = commands;

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

int last_command_status()
{
	return last_error_num;
}
