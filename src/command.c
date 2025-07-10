/* command.c
   Copyright (C) 2021-2025 Timo Kokkonen <tjko@iki.fi>

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
#include "pico/aon_timer.h"
#include "pico/rand.h"
#include "hardware/watchdog.h"
#include "cJSON.h"
#include "pico_sensor_lib.h"
#include "fanpico.h"
#ifdef WIFI_SUPPORT
#include "lwip/ip_addr.h"
#include "lwip/stats.h"
#include "pico_telnetd/util.h"
#endif


typedef int (*validate_str_func_t)(const char *args);

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



/**
 * Extract (unsigned) number from end of command string.
 *
 * If string is "MBFAN3" return value is 3.
 *
 * @param cmd command string
 *
 * @return number extracted from string (negative values indicates error)
 */
static int get_cmd_index(const char *cmd)
{
	const char *s;
	uint len;
	int idx;

	if (!cmd)
		return -1;

	s = cmd;
	len = strnlen(cmd, 256);
	if (len < 1 || len >= 256)
		return -2;

	/* Skip any spaces and letters at the beginning of the string... */
	while (len > 1) {
		if (isalpha((int)*s) || isblank((int)*s)) {
			s++;
			len--;
		} else {
			break;
		}
	}

	if (!str_to_int(s, &idx, 10))
		return -3;

	return idx;
}


/**
 * Return earlier (sub)command string
 *
 * If full command was "CONF:FAN2:HYST:PWM". Depth 0 returns "HYST"
 * and depth 1 returns "FAN2".
 *
 * @param prev_cmd Structure storing previous sub commands
 * @param depth Which command to return (0=last, 1=2nd to last, ...)
 *
 * @return (sub)command string
 */
static const char* get_prev_cmd(const struct prev_cmd_t *prev_cmd, uint depth)
{
	char *cmd;

	if (!prev_cmd)
		cmd = NULL;
	else if (depth >= prev_cmd->depth)
		cmd = NULL;
	else
		cmd = prev_cmd->cmds[prev_cmd->depth - depth - 1];

	return (cmd ? cmd : "");
}


/**
 * Return number from end of an earlier (sub)command
 *
 * If full command was "CONF:FAN3:NAME?", then depth 0 returns "FAN3"
 * and depth 1 returns "CONF".
 *
 * @param prev_cmd Structure storing previous sub commands
 * @param depth Which command to return (0=last, 1=2nd to last, ...)
 *
 * @return number extracted from specified subcommand (negative value indicates error)
 */
static int get_prev_cmd_index(const struct prev_cmd_t *prev_cmd, uint depth)
{
	int idx;

	if (!prev_cmd)
		return -1;
	if (depth >= prev_cmd->depth)
		return -2;

	idx = get_cmd_index(prev_cmd->cmds[prev_cmd->depth - depth - 1]);

	return idx;
}


/* Helper functions for commands */

static int string_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
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

static int bitmask16_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
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

static int uint32_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
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

static int uint8_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
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

static int bool_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
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
static int ip_change(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd, const char *name, ip_addr_t *ip)
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

static int ip_list_change(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd, const char *name, ip_addr_t *ip, uint32_t list_len)
{
	ip_addr_t tmpip;
	char buf[255], tmp[20];
	char *t, *arg, *saveptr;
	int idx = 0;


	if (query) {
		for (int i = 0; i < list_len; i++) {
			if (i > 0)
				printf(", ");
			printf("%s", ipaddr_ntoa(&ip[i]));
		}
		printf("\n");
	} else {
		if (!(arg = strdup(args)))
			return 2;
		t = strtok_r(arg, ",", &saveptr);
		while (t && idx < list_len) {
			if (ipaddr_aton(t, &tmpip)) {
				ip_addr_copy(ip[idx], tmpip);
				idx++;
			}

			t = strtok_r(NULL, ",", &saveptr);
		}
		free(arg);
		if (idx == 0)
			return 1;

		buf[0] = 0;
		for (int i = 0; i < list_len; i++) {
			if (i >= idx)
				ip_addr_set_zero(&ip[i]);
			snprintf(tmp, sizeof(tmp), "%s%s",
				(i > 0 ? ", " : ""), ipaddr_ntoa(&ip[i]));
			strncatenate(buf, tmp, sizeof(buf));
		}
		log_msg(LOG_NOTICE, "%s changed to '%s'", name, buf);
	}
	return 0;
}
#endif


/* Command functions */

int cmd_idn(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int i;
	pico_unique_board_id_t board_id;

	if (!query)
		return 1;

	printf("TJKO Industries,FANPICO-%s,", FANPICO_MODEL);
	pico_get_unique_board_id(&board_id);
	for (i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++)
		printf("%02x", board_id.id[i]);
	printf(",%s%s\n", FANPICO_VERSION, FANPICO_BUILD_TAG);

	return 0;
}

int cmd_exit(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (query)
		return 1;

#if WIFI_SUPPORT
	telnetserver_disconnect();
	sshserver_disconnect();
#endif
	return 0;
}

int cmd_who(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (!query)
		return 1;

#if WIFI_SUPPORT
	telnetserver_who();
	sshserver_who();
#endif
	return 0;
}

int cmd_usb_boot(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
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

int cmd_version(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	const char* credits = fanpico_credits_text;

	if (cmd && !query)
		return 1;

	printf("FanPico-%s v%s%s (%s; %s; SDK v%s; %s)\n\n",
		FANPICO_MODEL,
		FANPICO_VERSION,
		FANPICO_BUILD_TAG,
		__DATE__,
		PICO_CMAKE_BUILD_TYPE,
		PICO_SDK_VERSION_STRING,
		PICO_BOARD);

	if (query) {
		printf("%s\n", credits);
#ifdef __GNUC__
		printf("Compiled with: GCC v%s\n\n", __VERSION__);
#endif
	}

	return 0;
}

int cmd_fans(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (!query)
		return 1;

	printf("%d\n", FAN_COUNT);
	return 0;
}

int cmd_led(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
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

int cmd_mbfans(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (!query)
		return 1;

	printf("%d\n", MBFAN_COUNT);
	return 0;
}

int cmd_sensors(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (!query)
		return 1;

	printf("%d\n", SENSOR_COUNT);
	return 0;
}

int cmd_vsensors(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (!query)
		return 1;

	printf("%d\n", VSENSOR_COUNT);
	return 0;
}

int cmd_vsensors_sources(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (!query)
		return 1;

	for (int i = 0; i < VSENSOR_COUNT; i++) {
		const struct vsensor_input *v = &conf->vsensors[i];
		printf("vsensor%d,%s", i + 1, vsmode2str(v->mode));
		switch (v->mode) {
		case VSMODE_MANUAL:
			printf(",%0.2f,%ld", v->default_temp, v->timeout);
			break;
		case VSMODE_ONEWIRE:
			printf(",%016llx", v->onewire_addr);
			break;
		case VSMODE_I2C:
			printf(",0x%02x,%s", v->i2c_addr, i2c_sensor_type_str(v->i2c_type));
			break;
		default:
			for (int j = 0; j < VSENSOR_SOURCE_MAX_COUNT; j++) {
				if (v->sensors[j])
					printf(",%d", v->sensors[j]);
			}
			break;
		}
		printf("\n");
	}

	return 0;
}

int cmd_null(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	log_msg(LOG_INFO, "null command: %s %s (query=%d)", cmd, args, query);
	return 0;
}

int cmd_debug(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int level;

	if (query) {
		printf("%d\n", get_debug_level());
	} else if (str_to_int(args, &level, 10)) {
		set_debug_level((level < 0 ? 0 : level));
	}
	return 0;
}

int cmd_log_level(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
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

int cmd_syslog_level(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
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

int cmd_echo(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->local_echo, "Command Echo");
}

int cmd_display_type(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->display_type, sizeof(conf->display_type), "Display Type", NULL);
}

int cmd_display_theme(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->display_theme, sizeof(conf->display_theme), "Display Theme", NULL);
}

int cmd_display_logo(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->display_logo, sizeof(conf->display_logo), "Display Logo", NULL);
}

int cmd_display_layout_r(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->display_layout_r, sizeof(conf->display_layout_r),
			"Display Layout (Right)", NULL);
}

int cmd_reset(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
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

int cmd_save_config(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (query)
		return 1;
	save_config();
	return 0;
}

int cmd_print_config(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (!query)
		return 1;
	print_config();
	return 0;
}

int cmd_upload_config(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (query)
		return 1;
#if WATCHDOG_ENABLED
	watchdog_disable();
#endif
	upload_config();
#if WATCHDOG_ENABLED
	watchdog_enable(WATCHDOG_REBOOT_DELAY, 1);
#endif

	return 0;
}

int cmd_delete_config(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (query)
		return 1;
	delete_config();
	return 0;
}

int cmd_one(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (query)
		printf("1\n");
	return 0;
}

int cmd_zero(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (query)
		printf("0\n");
	return 0;
}

int cmd_read(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
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

int cmd_vsensors_read(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int i;

	if (!query)
		return 1;

	for (i = 0; i < VSENSOR_COUNT; i++) {
		printf("vsensor%d,\"%s\",%.1lf,%.0f,%0.0f\n", i+1,
			conf->vsensors[i].name,
			st->vtemp[i],
			st->vhumidity[i],
			st->vpressure[i]);
	}

	return 0;
}

int cmd_fan_name(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan;

	fan = get_prev_cmd_index(prev_cmd, 0) - 1;
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

int cmd_fan_min_pwm(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan, val;

	fan = get_prev_cmd_index(prev_cmd, 0) - 1;
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

int cmd_fan_max_pwm(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan, val;

	fan = get_prev_cmd_index(prev_cmd, 0) - 1;
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

int cmd_fan_pwm_coef(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan;
	float val;

	fan = get_prev_cmd_index(prev_cmd, 0) - 1;
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

int cmd_fan_pwm_map(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan, i, count;
	int val;
	char *arg, *t, *saveptr;
	struct pwm_map *map;
	struct pwm_map new_map;
	int ret = 0;

	fan = get_prev_cmd_index(prev_cmd, 0) - 1;
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
		if (!(arg = strdup(args)))
			return 2;
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

int cmd_fan_filter(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan;
	int ret = 0;
	char *tok, *saveptr, *param;
	struct fan_output *f;
	enum signal_filter_types new_filter;
	void *new_ctx;

	fan = get_prev_cmd_index(prev_cmd, 0) - 1;
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
		if (!(param = strdup(args)))
			return 2;
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

int cmd_fan_rpm_factor(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan;
	int val;

	fan = get_prev_cmd_index(prev_cmd, 0) - 1;
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

int cmd_fan_rpm_mode(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int ret = 0;
	int fan, val;
	struct fan_output *f;
	char *tok, *saveptr, *param;

	fan = get_prev_cmd_index(prev_cmd, 0) - 1;
	if (fan < 0 || fan >= FAN_COUNT)
		return 1;
	f = &conf->fans[fan];

	if (query) {
		printf("%s", rpm_mode2str(f->rpm_mode));
		if (f->rpm_mode == RMODE_LRA)
			printf(",%d,%d", f->lra_low, f->lra_high);
		printf("\n");
	} else {
		if (!(param = strdup(args)))
			return 2;
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

int cmd_fan_source(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan;
	int type, val, d_o, d_n;
	char *tok, *saveptr, *param;
	int ret = 0;

	fan = get_prev_cmd_index(prev_cmd, 0) - 1;
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
		if (!(param = strdup(args)))
			return 2;
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


int cmd_fan_tacho_hys(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan;
	float val;

	fan = get_prev_cmd_index(prev_cmd, 1) - 1;
	if (fan >= 0 && fan < FAN_COUNT) {
		if (query) {
			printf("%f\n", conf->fans[fan].tacho_hyst);
		} else if (str_to_float(args, &val)) {
			if (val >= 0.0) {
				log_msg(LOG_NOTICE, "fan%d: change tachometer hysteresis %f --> %f",
					fan + 1, conf->fans[fan].tacho_hyst, val);
				conf->fans[fan].tacho_hyst = val;
			} else {
				log_msg(LOG_WARNING, "fan%d: invalid new value for hysteresis: %f",
					fan + 1, val);
				return 2;
			}
		}
		return 0;
	}
	return 1;
}

int cmd_fan_pwm_hys(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan;
	float val;

	fan = get_prev_cmd_index(prev_cmd, 1) - 1;
	if (fan >= 0 && fan < FAN_COUNT) {
		if (query) {
			printf("%f\n", conf->fans[fan].pwm_hyst);
		} else if (str_to_float(args, &val)) {
			if (val >= 0.0) {
				log_msg(LOG_NOTICE, "fan%d: change PWM hysteresis %f --> %f",
					fan + 1, conf->fans[fan].pwm_hyst, val);
				conf->fans[fan].pwm_hyst = val;
			} else {
				log_msg(LOG_WARNING, "fan%d: invalid new value for hysteresis: %f",
					fan + 1, val);
				return 2;
			}
		}
		return 0;
	}
	return 1;
}

int cmd_fan_rpm(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan;
	double rpm;

	if (!query)
		return 1;

	fan = get_prev_cmd_index(prev_cmd, 0) - 1;
	if (fan >= 0 && fan < FAN_COUNT) {
		rpm = st->fan_freq[fan] * 60.0 / conf->fans[fan].rpm_factor;
		log_msg(LOG_DEBUG, "fan%d (tacho = %fHz) rpm = %.1lf", fan + 1,
			st->fan_freq[fan], rpm);
		printf("%.0lf\n", rpm);
		return 0;
	}

	return 1;
}

int cmd_fan_tacho(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan;
	float f;

	if (!query)
		return 1;

	fan = get_prev_cmd_index(prev_cmd, 0) - 1;
	if (fan >= 0 && fan < FAN_COUNT) {
		f = st->fan_freq[fan];
		log_msg(LOG_DEBUG, "fan%d tacho = %fHz", fan + 1, f);
		printf("%.1f\n", f);
		return 0;
	}

	return 1;
}

int cmd_fan_pwm(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan;
	float d;

	if (!query)
		return 1;

	fan = get_prev_cmd_index(prev_cmd, 0) - 1;
	if (fan >= 0 && fan < FAN_COUNT) {
		d = st->fan_duty[fan];
		log_msg(LOG_DEBUG, "fan%d duty = %f%%", fan + 1, d);
		printf("%.0f\n", d);
		return 0;
	}

	return 1;
}

int cmd_fan_read(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan;
	double rpm;
	float d, f;

	if (!query)
		return 1;

	if (!strncasecmp(get_prev_cmd(prev_cmd, 0), "fan", 3)) {
		fan = get_prev_cmd_index(prev_cmd, 0) - 1;
	} else {
		fan = get_cmd_index(cmd) - 1;
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

int cmd_mbfan_name(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int mbfan;

	mbfan = get_prev_cmd_index(prev_cmd, 0) - 1;
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

int cmd_mbfan_min_rpm(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int mbfan, val;

	mbfan = get_prev_cmd_index(prev_cmd, 0) - 1;
	if (mbfan >= 0 && mbfan < MBFAN_COUNT) {
		if (query) {
			printf("%d\n", conf->mbfans[mbfan].min_rpm);
		} else if (str_to_int(args, &val, 10)) {
			if (val >= 0 && val <= 50000) {
				log_msg(LOG_NOTICE, "mbfan%d: change min RPM %d --> %d", mbfan + 1,
					conf->mbfans[mbfan].min_rpm, val);
				conf->mbfans[mbfan].min_rpm = val;
			} else {
				log_msg(LOG_WARNING, "mbfan%d: invalid new value for min RPM: %d", mbfan + 1,
					val);
				return 2;
			}
		}
		return 0;
	}
	return 1;
}

int cmd_mbfan_max_rpm(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan, val;

	fan = get_prev_cmd_index(prev_cmd, 0) - 1;
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

int cmd_mbfan_rpm_coef(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan;
	float val;

	fan = get_prev_cmd_index(prev_cmd, 0) - 1;
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

int cmd_mbfan_rpm_factor(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan;
	int val;

	fan = get_prev_cmd_index(prev_cmd, 0) - 1;
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

int cmd_mbfan_rpm_map(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan, i, count;
	int val;
	char *arg, *t, *saveptr;
	struct tacho_map *map;
	struct tacho_map new_map;
	int ret = 0;

	fan = get_prev_cmd_index(prev_cmd, 0) - 1;
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
		if (!(arg = strdup(args)))
			return 2;
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

int cmd_mbfan_rpm_mode(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int ret = 0;
	int fan, val;
	char *tok, *saveptr, *param;
	struct mb_input *m;

	fan = get_prev_cmd_index(prev_cmd, 0) - 1;
	if (fan < 0 || fan >= MBFAN_COUNT)
		return 1;
	m = &conf->mbfans[fan];


	if (query) {
		printf("%s", rpm_mode2str(m->rpm_mode));
		if (m->rpm_mode == RMODE_LRA)
			printf(",%d,%s", m->lra_treshold, (m->lra_invert ? "HIGH" : "LOW"));
		printf("\n");
	} else {
		if (!(param = strdup(args)))
			return 2;
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

int cmd_mbfan_source(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan;
	int type, val, d_o, d_n, ocount;
	char *tok, *saveptr, *param;
	uint8_t new_sources[FAN_MAX_COUNT];
	enum tacho_source_types s_type;
	int ret = 0;

	memset(new_sources, 0, sizeof(new_sources));

	fan = get_prev_cmd_index(prev_cmd, 0) - 1;
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
		if (!(param = strdup(args)))
			return 2;
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

int cmd_mbfan_rpm(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan;
	double rpm;

	if (query) {
		fan = get_prev_cmd_index(prev_cmd, 0) - 1;
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

int cmd_mbfan_tacho(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan;
	float f;

	if (query) {
		fan = get_prev_cmd_index(prev_cmd, 0) - 1;
		if (fan >= 0 && fan < MBFAN_COUNT) {
			f = st->mbfan_freq[fan];
			log_msg(LOG_DEBUG, "mbfan%d tacho = %fHz", fan + 1, f);
			printf("%.1f\n", f);
			return 0;
		}
	}
	return 1;
}

int cmd_mbfan_pwm(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan;
	float d;

	if (query) {
		fan = get_prev_cmd_index(prev_cmd, 0) - 1;
		if (fan >= 0 && fan < MBFAN_COUNT) {
			d = st->mbfan_duty[fan];
			log_msg(LOG_DEBUG, "mbfan%d duty = %f%%", fan + 1, d);
			printf("%.0f\n", d);
			return 0;
		}
	}
	return 1;
}

int cmd_mbfan_read(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int fan;
	double rpm;
	float d, f;

	if (!query)
		return 1;

	if (!strncasecmp(get_prev_cmd(prev_cmd, 0), "mbfan", 5)) {
		fan = get_prev_cmd_index(prev_cmd, 0) - 1;
	} else {
		fan = get_cmd_index(cmd) - 1;
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

int cmd_mbfan_filter(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int mbfan;
	int ret = 0;
	char *tok, *saveptr, *param;
	struct mb_input *m;
	enum signal_filter_types new_filter;
	void *new_ctx;

	mbfan = get_prev_cmd_index(prev_cmd, 0) - 1;
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
		if (!(param = strdup(args)))
			return 2;
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

int cmd_sensor_name(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int sensor;

	sensor = get_prev_cmd_index(prev_cmd, 0) - 1;
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

int cmd_sensor_temp_offset(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int sensor;
	float val;

	sensor = get_prev_cmd_index(prev_cmd, 0) - 1;
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

int cmd_sensor_temp_coef(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int sensor;
	float val;

	sensor = get_prev_cmd_index(prev_cmd, 0) - 1;
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

int cmd_sensor_temp_nominal(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int sensor;
	float val;

	sensor = get_prev_cmd_index(prev_cmd, 0) - 1;
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

int cmd_sensor_ther_nominal(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int sensor;
	float val;

	sensor = get_prev_cmd_index(prev_cmd, 0) - 1;
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

int  cmd_sensor_adc_vref(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
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

int cmd_sensor_beta_coef(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int sensor;
	float val;

	sensor = get_prev_cmd_index(prev_cmd, 0) - 1;
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

int cmd_sensor_temp_map(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int sensor, i, count;
	float val;
	char *arg, *t, *saveptr;
	struct temp_map *map;
	struct temp_map new_map;
	int ret = 0;

	sensor = get_prev_cmd_index(prev_cmd, 0) - 1;
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
		if (!(arg = strdup(args)))
			return 2;
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

int cmd_sensor_temp(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int sensor;
	float d;

	if (!query)
		return 1;

	if (!strncasecmp(get_prev_cmd(prev_cmd, 0), "sensor", 6)) {
		sensor = get_prev_cmd_index(prev_cmd, 0) - 1;
	} else {
		sensor = get_cmd_index(cmd) - 1;
	}

	if (sensor >= 0 && sensor < SENSOR_COUNT) {
		d = st->temp[sensor];
		log_msg(LOG_DEBUG, "sensor%d temperature = %fC", sensor + 1, d);
		printf("%.0f\n", d);
		return 0;
	}

	return 1;
}

int cmd_sensor_filter(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int sensor;
	int ret = 0;
	char *tok, *saveptr, *param;
	struct sensor_input *s;
	enum signal_filter_types new_filter;
	void *new_ctx;

	sensor = get_prev_cmd_index(prev_cmd, 0) - 1;
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
		if (!(param = strdup(args)))
			return 2;
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

int cmd_vsensor_name(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int sensor;

	sensor = get_prev_cmd_index(prev_cmd, 0) - 1;
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

int cmd_vsensor_source(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	struct vsensor_input *v;
	int sensor, val, i;
	uint8_t vsmode, selected[VSENSOR_SOURCE_MAX_COUNT];
	float default_temp;
	int timeout;
	char *tok, *saveptr, *param, temp_str[32], tmp[8];
	int ret = 0;
	int count = 0;

	sensor = get_prev_cmd_index(prev_cmd, 0) - 1;
	if (sensor < 0 || sensor >= VSENSOR_COUNT)
		return 1;
	v = &conf->vsensors[sensor];

	if (query) {
		printf("%s", vsmode2str(v->mode));
		if (v->mode == VSMODE_MANUAL) {
			printf(",%0.2f,%ld", v->default_temp, v->timeout);
		} else if (v->mode == VSMODE_ONEWIRE) {
			printf(",%016llx", v->onewire_addr);
		} else if (v->mode == VSMODE_I2C) {
			printf(",0x%02x,%s", v->i2c_addr, i2c_sensor_type_str(v->i2c_type));
		} else {
			for(i = 0; i < VSENSOR_SOURCE_MAX_COUNT; i++) {
				if (v->sensors[i])
					printf(",%d", v->sensors[i]);
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
						v->mode = vsmode;
						v->default_temp = default_temp;
						v->timeout = timeout;
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
					v->mode = vsmode;
					v->onewire_addr = addr;
					ret = 0;
				}
			} else if (vsmode == VSMODE_I2C) {
				tok = strtok_r(NULL, ",", &saveptr);
				if (str_to_int(tok, &val, 16)) {
					if (val > 0 && val < 128 && !i2c_reserved_address(val)) {
						tok = strtok_r(NULL, ",", &saveptr);
						uint type = get_i2c_sensor_type(tok);
						if (type > 0) {
							log_msg(LOG_NOTICE, "vsensor%d: set source to %s,0x%02x,%s",
								sensor + 1,
								vsmode2str(vsmode),
								val,
								i2c_sensor_type_str(type));
							v->mode = vsmode;
							v->i2c_type = type;
							v->i2c_addr = val;
							ret = 0;
						}
					}
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
					v->mode = vsmode;
					for(i = 0; i < SENSOR_COUNT; i++) {
						v->sensors[i] = selected[i];
					}
					ret = 0;
				}
			}
		}
		free(param);
	}

	return ret;
}

int cmd_vsensor_temp_map(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int sensor, i, count;
	float val;
	char *arg, *t, *saveptr;
	struct temp_map *map;
	struct temp_map new_map;
	int ret = 0;

	sensor = get_prev_cmd_index(prev_cmd, 0) - 1;
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
		if (!(arg = strdup(args)))
			return 2;
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

int cmd_vsensor_temp(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int sensor;
	float d;

	if (!query)
		return 1;

	if (!strncasecmp(get_prev_cmd(prev_cmd, 0), "vsensor", 7)) {
		sensor = get_prev_cmd_index(prev_cmd, 0) - 1;
	} else {
		sensor = get_cmd_index(cmd) - 1;
	}

	if (sensor >= 0 && sensor < VSENSOR_COUNT) {
		d = st->vtemp[sensor];
		log_msg(LOG_DEBUG, "vsensor%d temperature = %fC", sensor + 1, d);
		printf("%.0f\n", d);
		return 0;
	}

	return 1;
}

int cmd_vsensor_humidity(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int sensor;
	float d;

	if (!query)
		return 1;

	sensor = get_prev_cmd_index(prev_cmd, 0) - 1;
	if (sensor >= 0 && sensor < VSENSOR_COUNT) {
		d = st->vhumidity[sensor];
		log_msg(LOG_DEBUG, "vsensor%d humidity = %f%%", sensor + 1, d);
		printf("%.0f\n", d);
		return 0;
	}

	return 1;
}

int cmd_vsensor_pressure(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int sensor;
	float d;

	if (!query)
		return 1;

	sensor = get_prev_cmd_index(prev_cmd, 0) - 1;
	if (sensor >= 0 && sensor < VSENSOR_COUNT) {
		d = st->vpressure[sensor];
		log_msg(LOG_DEBUG, "vsensor%d pressure = %fhPa", sensor + 1, d);
		printf("%.0f\n", d);
		return 0;
	}

	return 1;
}

int cmd_vsensor_filter(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int sensor;
	int ret = 0;
	char *tok, *saveptr, *param;
	struct vsensor_input *s;
	enum signal_filter_types new_filter;
	void *new_ctx;

	sensor = get_prev_cmd_index(prev_cmd, 0) - 1;
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
		if (!(param = strdup(args)))
			return 2;
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

int cmd_vsensor_write(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int sensor;
	float val;

	if (query)
		return 1;

	if (!strncasecmp(get_prev_cmd(prev_cmd, 0), "vsensor", 7)) {
		sensor = get_prev_cmd_index(prev_cmd, 0) - 1;
	} else {
		sensor = get_cmd_index(cmd) - 1;
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

int cmd_wifi(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
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
int cmd_wifi_ip(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return ip_change(cmd, args, query, prev_cmd, "IP", &conf->ip);
}

int cmd_wifi_netmask(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return ip_change(cmd, args, query, prev_cmd, "Netmask", &conf->netmask);
}

int cmd_wifi_gateway(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return ip_change(cmd, args, query, prev_cmd, "Default Gateway", &conf->gateway);
}

int cmd_wifi_dns(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return ip_list_change(cmd, args, query, prev_cmd, "DNS Servers", conf->dns_servers,
		DNS_MAX_SERVERS);
}

int cmd_wifi_syslog(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return ip_change(cmd, args, query, prev_cmd, "Syslog Server", &conf->syslog_server);
}

int cmd_wifi_ntp(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return ip_change(cmd, args, query, prev_cmd, "NTP Server", &conf->ntp_server);
}

int cmd_wifi_mac(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (query) {
		printf("%s\n", mac_address_str(net_state->mac));
		return 0;
	}
	return 1;
}

int cmd_wifi_ssid(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->wifi_ssid, sizeof(conf->wifi_ssid), "WiFi SSID", NULL);
}

int cmd_wifi_status(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (query) {
		wifi_status();
		return 0;
	}
	return 1;
}

int cmd_wifi_stats(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (query) {
		stats_display();
		return 0;
	}
	return 1;
}

int cmd_wifi_info(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (query) {
		wifi_info_display();
		return 0;
	}
	return 1;
}

int cmd_wifi_rejoin(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (!query) {
		wifi_rejoin();
		return 0;
	}
	return 1;
}

int cmd_wifi_country(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->wifi_country, sizeof(conf->wifi_country),
			"WiFi Country", valid_wifi_country);
}

int cmd_wifi_password(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->wifi_passwd, sizeof(conf->wifi_passwd), "WiFi Password", NULL);
}

int cmd_wifi_hostname(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->hostname, sizeof(conf->hostname),
			"WiFi Hostname", valid_hostname);
}

int cmd_wifi_auth_mode(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	uint32_t type;

	if (query) {
		printf("%s\n", conf->wifi_auth_mode);
	} else {
		if (!wifi_get_auth_type(args, &type))
			return 1;
		if (strncmp(conf->wifi_auth_mode, args, sizeof(conf->wifi_auth_mode))) {
			log_msg(LOG_NOTICE, "WiFi Auth Type change %s --> %s",
				conf->wifi_auth_mode, args);
			strncopy(conf->wifi_auth_mode, args, sizeof(conf->wifi_auth_mode));
		}
	}

	return 0;
}

int cmd_wifi_mode(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return uint8_setting(cmd, args, query, prev_cmd,
			&conf->wifi_mode, 0, 1, "WiFi Mode");
}

int cmd_mqtt_server(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_server, sizeof(conf->mqtt_server), "MQTT Server", NULL);
}

int cmd_mqtt_port(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return uint32_setting(cmd, args, query, prev_cmd,
			&conf->mqtt_port, 0, 65535, "MQTT Port");
}

int cmd_mqtt_user(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_user, sizeof(conf->mqtt_user), "MQTT User", NULL);
}

int cmd_mqtt_pass(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_pass, sizeof(conf->mqtt_pass), "MQTT Password", NULL);
}

int cmd_mqtt_status_interval(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return uint32_setting(cmd, args, query, prev_cmd,
			&conf->mqtt_status_interval, 0, (86400 * 30), "MQTT Publish Status Interval");
}

int cmd_mqtt_temp_interval(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return uint32_setting(cmd, args, query, prev_cmd,
			&conf->mqtt_temp_interval, 0, (86400 * 30), "MQTT Publish Temp Interval");
}

int cmd_mqtt_vsensor_interval(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return uint32_setting(cmd, args, query, prev_cmd,
			&conf->mqtt_vsensor_interval, 0, (86400 * 30), "MQTT Publish VSENSOR Interval");
}

int cmd_mqtt_rpm_interval(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return uint32_setting(cmd, args, query, prev_cmd,
			&conf->mqtt_rpm_interval, 0, (86400 * 30), "MQTT Publish RPM Interval");
}

int cmd_mqtt_duty_interval(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return uint32_setting(cmd, args, query, prev_cmd,
			&conf->mqtt_duty_interval, 0, (86400 * 30), "MQTT Publish PWM Interval");
}

int cmd_mqtt_allow_scpi(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->mqtt_allow_scpi, "MQTT Allow SCPI Commands");
}

int cmd_mqtt_status_topic(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_status_topic, sizeof(conf->mqtt_status_topic),
			"MQTT Status Topic", NULL);
}

int cmd_mqtt_cmd_topic(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_cmd_topic, sizeof(conf->mqtt_cmd_topic),
			"MQTT Command Topic", NULL);
}

int cmd_mqtt_resp_topic(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_resp_topic,
			sizeof(conf->mqtt_resp_topic), "MQTT Response Topic", NULL);
}

int cmd_mqtt_temp_topic(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_temp_topic,
			sizeof(conf->mqtt_temp_topic), "MQTT Temperature Topic", NULL);
}

int cmd_mqtt_vtemp_topic(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_vtemp_topic,
			sizeof(conf->mqtt_vtemp_topic), "MQTT VTemperature Topic", NULL);
}

int cmd_mqtt_vhumidity_topic(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_vhumidity_topic,
			sizeof(conf->mqtt_vhumidity_topic), "MQTT VHumidity Topic", NULL);
}

int cmd_mqtt_vpressure_topic(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_vpressure_topic,
			sizeof(conf->mqtt_vpressure_topic), "MQTT VPressure Topic", NULL);
}

int cmd_mqtt_fan_rpm_topic(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_fan_rpm_topic,
			sizeof(conf->mqtt_fan_rpm_topic), "MQTT Fan RPM Topic", NULL);
}

int cmd_mqtt_fan_duty_topic(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_fan_duty_topic,
			sizeof(conf->mqtt_fan_duty_topic), "MQTT Fan PWM Topic", NULL);
}

int cmd_mqtt_mbfan_rpm_topic(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_mbfan_rpm_topic,
			sizeof(conf->mqtt_mbfan_rpm_topic), "MQTT MBFan RPM Topic", NULL);
}

int cmd_mqtt_mbfan_duty_topic(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_mbfan_duty_topic,
			sizeof(conf->mqtt_mbfan_duty_topic), "MQTT MBFan PWM Topic", NULL);
}

int cmd_mqtt_ha_discovery(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->mqtt_ha_discovery_prefix,
			sizeof(conf->mqtt_ha_discovery_prefix), "MQTT Home Assistant Discovery Prefix", NULL);
}

int cmd_mqtt_mask_temp(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return bitmask16_setting(cmd, args, query, prev_cmd,
				&conf->mqtt_temp_mask, SENSOR_MAX_COUNT,
				1, "MQTT Temperature Mask");
}

int cmd_mqtt_mask_vtemp(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return bitmask16_setting(cmd, args, query, prev_cmd,
				&conf->mqtt_vtemp_mask, VSENSOR_MAX_COUNT,
				1, "MQTT VSENSOR Temperature Mask");
}

int cmd_mqtt_mask_vhumidity(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return bitmask16_setting(cmd, args, query, prev_cmd,
				&conf->mqtt_vhumidity_mask, VSENSOR_MAX_COUNT,
				1, "MQTT VSENSOR Humidity Mask");
}

int cmd_mqtt_mask_vpressure(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return bitmask16_setting(cmd, args, query, prev_cmd,
				&conf->mqtt_vpressure_mask, VSENSOR_MAX_COUNT,
				1, "MQTT VSENSOR Pressure Mask");
}

int cmd_mqtt_mask_fan_rpm(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return bitmask16_setting(cmd, args, query, prev_cmd,
				&conf->mqtt_fan_rpm_mask, FAN_MAX_COUNT,
				1, "MQTT Fan RPM Mask");
}

int cmd_mqtt_mask_fan_duty(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return bitmask16_setting(cmd, args, query, prev_cmd,
				&conf->mqtt_fan_duty_mask, FAN_MAX_COUNT,
				1, "MQTT Fan PWM Mask");
}

int cmd_mqtt_mask_mbfan_rpm(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return bitmask16_setting(cmd, args, query, prev_cmd,
				&conf->mqtt_mbfan_rpm_mask, MBFAN_MAX_COUNT,
				1, "MQTT MBFan RPM Mask");
}

int cmd_mqtt_mask_mbfan_duty(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return bitmask16_setting(cmd, args, query, prev_cmd,
				&conf->mqtt_mbfan_duty_mask, MBFAN_MAX_COUNT,
				1, "MQTT MBFan PWM Mask");
}

#if TLS_SUPPORT
int cmd_mqtt_tls(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->mqtt_tls, "MQTT TLS Mode");
}

int cmd_tls_pkey(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
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

int cmd_tls_cert(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
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

int cmd_ssh_auth(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->ssh_auth, "SSH Server Authentication");
}

int cmd_ssh_server(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->ssh_active, "SSH Server");
}

int cmd_ssh_port(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return uint32_setting(cmd, args, query, prev_cmd,
			&conf->ssh_port, 0, 65535, "SSH Port");
}

int cmd_ssh_user(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->ssh_user, sizeof(conf->ssh_user),
			"SSH Username", NULL);
}

int cmd_ssh_pass(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (query) {
		printf("%s\n", cfg->ssh_pwhash);
		return 0;
	}

	if (strlen(args) > 0) {
		strncopy(conf->ssh_pwhash, generate_sha512crypt_pwhash(args),
			sizeof(conf->ssh_pwhash));
	} else {
		conf->ssh_pwhash[0] = 0;
		log_msg(LOG_NOTICE, "SSH password removed.");
	}
	return 0;
}

int cmd_ssh_pkey(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (!query)
		return 1;

	ssh_list_pkeys();
	return 0;
}

int cmd_ssh_pkey_create(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (query)
		return 1;

	return ssh_create_pkey(args);
}

int cmd_ssh_pkey_del(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (query)
		return 1;

	return ssh_delete_pkey(args);
}

int cmd_ssh_pubkey(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int count = 0;
	char tmp[255];

	if (!query)
		return 1;

	for (int i = 0; i < SSH_MAX_PUB_KEYS; i++) {
		struct ssh_public_key *k = &conf->ssh_pub_keys[i];

		if (k->pubkey_size == 0 || strlen(k->username) < 1)
			continue;
		printf("%d: %s, %s\n", ++count, k->username,
			ssh_pubkey_to_str(k, tmp, sizeof(tmp)));
	}
	if (count < 1) {
		printf("No SSH (authentication) public keys found.\n");
	}

	return 0;
}

int cmd_ssh_pubkey_add(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	struct ssh_public_key pubkey;
	char *s, *username, *pkey, *saveptr;
	int res = 0;

	if (query)
		return 1;

	if (!(s = strdup(args)))
		return 2;

	if ((username = strtok_r(s, " ", &saveptr))) {
		username = trim_str(username);
		if (strlen(username) < 1)
			username = NULL;
	}

	if ((pkey = strtok_r(NULL, ",", &saveptr))) {
		pkey = trim_str(pkey);
		if (str_to_ssh_pubkey(pkey, &pubkey))
			pkey = NULL;
	}

	if (username && pkey) {
		int idx = -1;

		/* Check for first available slot */
		for(int i = 0; i < SSH_MAX_PUB_KEYS; i++) {
			if (conf->ssh_pub_keys[i].pubkey_size == 0) {
				idx = i;
				break;
			}
		}

		if (idx < 0) {
			printf("Maximum number of public keys already added.\n");
			res = 2;
		} else {
			strncopy(pubkey.username, username, sizeof(pubkey.username));
			conf->ssh_pub_keys[idx] = pubkey;
			log_msg(LOG_INFO, "SSH Public key added: slot %d: %s (%s)\n", idx + 1,
				pubkey.type, pubkey.username);
		}
	}

	free(s);

	return res;
}

int cmd_ssh_pubkey_del(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	int idx;

	if (query)
		return 1;

	if (!str_to_int(args, &idx, 10))
		return 2;
	if (idx < 1 || idx > SSH_MAX_PUB_KEYS)
		return 2;

	idx--;
	if (conf->ssh_pub_keys[idx].pubkey_size > 0) {
		log_msg(LOG_INFO, "SSH Public key deleted: slot %d: %s:%s (%s)\n", idx + 1,
			conf->ssh_pub_keys[idx].username,
			conf->ssh_pub_keys[idx].type,
			conf->ssh_pub_keys[idx].name);
		memset(&conf->ssh_pub_keys[idx], 0, sizeof(struct ssh_public_key));
	}

	return 0;
}

int cmd_telnet_auth(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->telnet_auth, "Telnet Server Authentication");
}

int cmd_telnet_rawmode(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->telnet_raw_mode, "Telnet Server Raw Mode");
}

int cmd_telnet_server(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->telnet_active, "Telnet Server");
}

int cmd_telnet_port(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return uint32_setting(cmd, args, query, prev_cmd,
			&conf->telnet_port, 0, 65535, "Telnet Port");
}

int cmd_telnet_user(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->telnet_user, sizeof(conf->telnet_user),
			"Telnet Username", NULL);
}

int cmd_telnet_pass(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
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

int cmd_snmp_agent(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->snmp_active, "SNMP Agent");
}

int cmd_snmp_community(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->snmp_community, sizeof(conf->snmp_community),
			"SNMP Community", NULL);
}

int cmd_snmp_community_write(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->snmp_community_write, sizeof(conf->snmp_community_write),
			"SNMP Community (Write)", NULL);
}

int cmd_snmp_contact(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->snmp_contact, sizeof(conf->snmp_contact),
			"SNMP sysContact", NULL);
}

int cmd_snmp_location(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->snmp_location, sizeof(conf->snmp_location),
			"SNMP sysLocation", NULL);
}

int cmd_snmp_auth_traps(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->snmp_auth_traps, "SNMP Authentication Traps");
}

int cmd_snmp_community_trap(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->snmp_community_trap, sizeof(conf->snmp_community_trap),
			"SNMP Traps Community", NULL);
}

int cmd_snmp_trap_dst(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return ip_change(cmd, args, query, prev_cmd, "SNMP Trap Destination", &conf->snmp_trap_dst);
}
#endif /* WIFI_SUPPOERT */

int cmd_time(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	struct timespec ts;
	time_t t;
	char buf[32];

	if (query) {
		if (aon_timer_is_running()) {
			aon_timer_get_time(&ts);
			time_t_to_str(buf, sizeof(buf), timespec_to_time_t(&ts));
			printf("%s\n", buf);
		}
		return 0;
	}

	if (str_to_time_t(args, &t)) {
		time_t_to_timespec(t, &ts);
		if (aon_timer_is_running()) {
			aon_timer_set_time(&ts);
		} else {
			aon_timer_start(&ts);
		}
		time_t_to_str(buf, sizeof(buf), t);
		log_msg(LOG_NOTICE, "Set system clock: %s", buf);
		return 0;
	}

	return 2;
}

int cmd_timezone(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->timezone, sizeof(conf->timezone), "Timezone", NULL);
}

int cmd_uptime(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	const struct persistent_memory_block *m = persistent_mem;
	struct timespec ts;
	uint64_t uptime;
	char buf[32];

	if (!query)
		return 1;


	if (aon_timer_is_running()) {
		if (aon_timer_get_time(&ts)) {
			time_t_to_str(buf, sizeof(buf), timespec_to_time_t(&ts));
			printf("%s ", buf + 11);
		}
	}

	uptime = to_us_since_boot(get_absolute_time());
	uptime_to_str(buf, sizeof(buf), uptime, false);
	printf("up %s%s\n", buf,
		(rebooted_by_watchdog ? " [rebooted by watchdog]" : ""));
	uptime_to_str(buf, sizeof(buf), uptime + m->total_uptime, false);
	printf("since cold boot %s (soft reset count: %lu)\n",
		buf, m->warmstart);
	return 0;
}

int cmd_err(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
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

int cmd_name(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return string_setting(cmd, args, query, prev_cmd,
			conf->name, sizeof(conf->name), "System Name", NULL);
}


int cmd_lfs(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
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

int cmd_lfs_del(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (query)
		return 1;

	if (strlen(args) < 1)
		return 2;

	if (flash_delete_file(args))
		return 2;

	return 0;
}

int cmd_lfs_dir(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (!query)
		return 1;

	flash_list_directory("/", true);

	return 0;
}

int cmd_lfs_format(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (query)
		return 1;

	printf("Formatting flash filesystem...\n");
	if (flash_format(true))
		return 2;
	printf("Filesystem successfully formatted.\n");

	return 0;
}

int cmd_lfs_ren(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	char *saveptr, *oldname, *newname, *arg;
	int res = 2;

	if (query)
		return 1;

	if (!(arg = strdup(args)))
		return 2;

	oldname = strtok_r(arg, " \t", &saveptr);
	if (oldname && strlen(oldname) > 0) {
		newname = strtok_r(NULL, " \t", &saveptr);
		if (newname && strlen(newname) > 0) {
			if (!flash_rename_file(oldname, newname)) {
				res = 0;
			}
		}
	}
	free(arg);

	return res;
}

int cmd_lfs_copy(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	char *saveptr, *srcname, *dstname, *arg;
	int res = 2;

	if (query)
		return 1;

	if (!(arg = strdup(args)))
		return 2;

	srcname = strtok_r(arg, " \t", &saveptr);
	if (srcname && strlen(srcname) > 0) {
		dstname = strtok_r(NULL, " \t", &saveptr);
		if (dstname && strlen(dstname) > 0) {
			if (strcmp(srcname, dstname)) {
				if (!flash_copy_file(srcname, dstname, false)) {
					res = 0;
				}
			}
		}
	}
	free(arg);

	return res;
}

int cmd_flash(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (!query)
		return 1;

	print_rp2040_flashinfo();
	return 0;
}


#define TEST_MEM_SIZE (1024*1024)

int cmd_memory(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
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
	if (buf)
		free(buf);

	/* Test how much memory available in 'blocksize' blocks... */
	int i = 0;
	int max = TEST_MEM_SIZE / blocksize + 1;
	size_t refbufsize = max * sizeof(void*);
	void **refbuf = malloc(refbufsize);
	if (refbuf) {
		memset(refbuf, 0, refbufsize);
		while (i < max) {
			if (!(refbuf[i] = malloc(blocksize)))
				break;
			i++;
		}
	}
	printf("Total available memory:                %u bytes\n",
		i * blocksize + refbufsize);
	if (refbuf) {
		i = 0;
		while (i < max && refbuf[i]) {
			free(refbuf[i++]);
		}
		free(refbuf);
	}
	return 0;
}

int cmd_onewire(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->onewire_active, "1-Wire Bus status");
}

int cmd_onewire_sensors(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
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

int cmd_i2c(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (!query)
		return 1;

	display_i2c_status();
	return 0;
}

int cmd_i2c_scan(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	if (!query)
		return 1;

	scan_i2c_bus();
	return 0;
}

int cmd_i2c_speed(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return uint32_setting(cmd, args, query, prev_cmd,
			&conf->i2c_speed, 10000, 3400000, "I2C Bus Speed (Hz)");
}

int cmd_serial(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
{
	return bool_setting(cmd, args, query, prev_cmd,
			&conf->serial_active, "Serial Console status");
}

int cmd_spi(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd)
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
	{ "COPY",      4, NULL,              cmd_lfs_copy },
	{ "DELete",    3, NULL,              cmd_lfs_del },
	{ "DIRectory", 3, NULL,              cmd_lfs_dir },
	{ "FORMAT",    6, NULL,              cmd_lfs_format },
	{ "REName",    3, NULL,              cmd_lfs_ren },
	{ 0, 0, 0, 0 }
};

const struct cmd_t wifi_commands[] = {
#ifdef WIFI_SUPPORT
	{ "AUTHmode",  4, NULL,              cmd_wifi_auth_mode },
	{ "COUntry",   3, NULL,              cmd_wifi_country },
	{ "GATEway",   4, NULL,              cmd_wifi_gateway },
	{ "HOSTname",  4, NULL,              cmd_wifi_hostname },
	{ "IPaddress", 2, NULL,              cmd_wifi_ip },
	{ "MAC",       3, NULL,              cmd_wifi_mac },
	{ "NETMask",   4, NULL,              cmd_wifi_netmask },
	{ "DNS",       3, NULL,              cmd_wifi_dns },
	{ "NTP",       3, NULL,              cmd_wifi_ntp },
	{ "MODE",      4, NULL,              cmd_wifi_mode },
	{ "PASSword",  4, NULL,              cmd_wifi_password },
	{ "REJOIN",    6, NULL,              cmd_wifi_rejoin },
	{ "SSID",      4, NULL,              cmd_wifi_ssid },
	{ "STATS",     5, NULL,              cmd_wifi_stats },
	{ "STATus",    4, NULL,              cmd_wifi_status },
	{ "INFO",      4, NULL,              cmd_wifi_info },
	{ "SYSLOG",    6, NULL,              cmd_wifi_syslog },
#endif
	{ 0, 0, 0, 0 }
};

#ifdef WIFI_SUPPORT
const struct cmd_t mqtt_mask_commands[] = {
	{ "TEMP",      4, NULL,              cmd_mqtt_mask_temp },
	{ "VTEMP",     5, NULL,              cmd_mqtt_mask_vtemp },
	{ "VHUMidity", 4, NULL,              cmd_mqtt_mask_vhumidity },
	{ "VPREssure", 4, NULL,              cmd_mqtt_mask_vpressure },
	{ "FANRPM",    6, NULL,              cmd_mqtt_mask_fan_rpm },
	{ "FANPWM",    6, NULL,              cmd_mqtt_mask_fan_duty },
	{ "MBFANRPM",  8, NULL,              cmd_mqtt_mask_mbfan_rpm },
	{ "MBFANPWM",  8, NULL,              cmd_mqtt_mask_mbfan_duty },
	{ 0, 0, 0, 0 }
};

const struct cmd_t mqtt_interval_commands[] = {
	{ "STATUS",    6, NULL,              cmd_mqtt_status_interval },
	{ "TEMP",      4, NULL,              cmd_mqtt_temp_interval },
	{ "VSENsor",   4, NULL,              cmd_mqtt_vsensor_interval },
	{ "RPM",       3, NULL,              cmd_mqtt_rpm_interval },
	{ "PWM",       3, NULL,              cmd_mqtt_duty_interval },
	{ 0, 0, 0, 0 }
};

const struct cmd_t mqtt_topic_commands[] = {
	{ "STATus",    4, NULL,              cmd_mqtt_status_topic },
	{ "COMMand",   4, NULL,              cmd_mqtt_cmd_topic },
	{ "RESPonse",  4, NULL,              cmd_mqtt_resp_topic },
	{ "TEMP",      4, NULL,              cmd_mqtt_temp_topic },
	{ "VTEMP",     5, NULL,              cmd_mqtt_vtemp_topic },
	{ "VHUMidity", 4, NULL,              cmd_mqtt_vhumidity_topic },
	{ "VPREssure", 4, NULL,              cmd_mqtt_vpressure_topic },
	{ "FANRPM",    6, NULL,              cmd_mqtt_fan_rpm_topic },
	{ "FANPWM",    6, NULL,              cmd_mqtt_fan_duty_topic },
	{ "MBFANRPM",  8, NULL,              cmd_mqtt_mbfan_rpm_topic },
	{ "MBFANPWM",  8, NULL,              cmd_mqtt_mbfan_duty_topic },
	{ 0, 0, 0, 0 }
};

const struct cmd_t mqtt_ha_commands[] = {
	{ "DISCovery", 4, NULL,              cmd_mqtt_ha_discovery },
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
	{ "HA",        2, mqtt_ha_commands, NULL },
	{ "INTerval",  3, mqtt_interval_commands, NULL },
	{ "MASK",      4, mqtt_mask_commands, NULL },
	{ "TOPIC",     5, mqtt_topic_commands, NULL },
	{ 0, 0, 0, 0 }
};

const struct cmd_t snmp_trap_commands[] = {
	{ "AUTH",      4, NULL,              cmd_snmp_auth_traps },
	{ "COMMunity", 4, NULL,              cmd_snmp_community_trap },
	{ "DESTination", 4, NULL,            cmd_snmp_trap_dst },
	{ 0, 0, 0, 0 }
};

const struct cmd_t snmp_commands[] = {
	{ "AGENT",     5, NULL,              cmd_snmp_agent },
	{ "COMMunity", 4, NULL,              cmd_snmp_community },
	{ "CONTact",   4, NULL,              cmd_snmp_contact },
	{ "LOCAtion",  4, NULL,              cmd_snmp_location },
	{ "TRAPs",     4, snmp_trap_commands, NULL },
	{ "WRITecommunity", 4, NULL,         cmd_snmp_community_write },
	{ 0, 0, 0, 0 }
};


const struct cmd_t ssh_pkey_commands[] = {
	{ "CREate",    3, NULL,              cmd_ssh_pkey_create },
	{ "DELete",    3, NULL,              cmd_ssh_pkey_del },
	{ "LIST",      4, NULL,              cmd_ssh_pkey },
	{ 0, 0, 0, 0 }
};

const struct cmd_t ssh_pubkey_commands[] = {
	{ "ADD",       3, NULL,              cmd_ssh_pubkey_add },
	{ "DELete",    3, NULL,              cmd_ssh_pubkey_del },
	{ "LIST",      4, NULL,              cmd_ssh_pubkey },
	{ 0, 0, 0, 0 }
};

const struct cmd_t ssh_commands[] = {
	{ "AUTH",      4, NULL,              cmd_ssh_auth },
	{ "PORT",      4, NULL,              cmd_ssh_port },
	{ "SERVer",    4, NULL,              cmd_ssh_server },
	{ "PASSword",  4, NULL,              cmd_ssh_pass },
	{ "USER",      4, NULL,              cmd_ssh_user },
	{ "KEY",       3, ssh_pkey_commands, cmd_ssh_pkey },
	{ "PUBKEY",    6, ssh_pubkey_commands, cmd_ssh_pubkey },
	{ 0, 0, 0, 0 }
};

const struct cmd_t telnet_commands[] = {
	{ "AUTH",      4, NULL,              cmd_telnet_auth },
	{ "PORT",      4, NULL,              cmd_telnet_port },
	{ "RAWmode",   3, NULL,              cmd_telnet_rawmode },
	{ "SERVer",    4, NULL,              cmd_telnet_server },
	{ "PASSword",  4, NULL,              cmd_telnet_pass },
	{ "USER",      4, NULL,              cmd_telnet_user },
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


const struct cmd_t i2c_commands[] = {
	{ "SCAN",      4, NULL,              cmd_i2c_scan },
	{ "SPEED",     5, NULL,              cmd_i2c_speed },
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
	{ "I2C",       3, i2c_commands,      cmd_i2c },
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
#if WIFI_SUPPORT
	{ "SNMP",      4, snmp_commands,     NULL },
	{ "TELNET",    6, telnet_commands,   NULL },
	{ "SSH",       3, ssh_commands,      NULL },
#endif
	{ "SPI",       3, NULL,              cmd_spi },
	{ "SYSLOG",    6, NULL,              cmd_syslog_level },
	{ "TIMEZONE",  8, NULL,              cmd_timezone },
	{ "TIME",      4, NULL,              cmd_time },
	{ "TLS",       3, tls_commands,      NULL },
	{ "UPGRADE",   7, NULL,              cmd_usb_boot },
	{ "UPTIme",    4, NULL,              cmd_uptime },
	{ "VERsion",   3, NULL,              cmd_version },
	{ "VREFadc",   4, NULL,              cmd_sensor_adc_vref },
	{ "VSENSORS",  8, NULL,              cmd_vsensors },
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
	{ "HYSTeresis",4, fan_hyst_commands, NULL },
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

const struct cmd_t vsensors_c_commands[] = {
	{ "SOUrce",   3, NULL,               cmd_vsensors_sources },
	{ 0, 0, 0, 0 }
};

const struct cmd_t config_commands[] = {
	{ "DELete",    3, NULL,              cmd_delete_config },
	{ "FAN",       3, fan_c_commands,    NULL },
	{ "MBFAN",     5, mbfan_c_commands,  NULL },
	{ "Read",      1, NULL,              cmd_print_config },
	{ "SAVe",      3, NULL,              cmd_save_config },
	{ "SENSOR",    6, sensor_c_commands, NULL },
	{ "UPLOAD",    6, NULL,              cmd_upload_config },
	{ "VSENSORS",  8, vsensors_c_commands, cmd_vsensors_sources },
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
	{ "HUMidity",  3, NULL,              cmd_vsensor_humidity },
	{ "PREssure",  3, NULL,              cmd_vsensor_pressure },
	{ "Read",      1, NULL,              cmd_vsensor_temp },
	{ "TEMP",      4, NULL,              cmd_vsensor_temp },
	{ 0, 0, 0, 0 }
};

const struct cmd_t measure_commands[] = {
	{ "FAN",       3, fan_commands,      cmd_fan_read },
	{ "MBFAN",     5, mbfan_commands,    cmd_mbfan_read },
	{ "Read",      1, NULL,              cmd_read },
	{ "SENSOR",    6, sensor_commands,   cmd_sensor_temp },
	{ "VSENSORS",  8, NULL,              cmd_vsensors_read },
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
	{ "EXIT",      4, NULL,              cmd_exit },
	{ "MEAsure",   3, measure_commands,  NULL },
	{ "SYStem",    3, system_commands,   NULL },
	{ "Read",      1, NULL,              cmd_read },
	{ "WHO",       3, NULL,              cmd_who },
	{ "WRIte",     3, write_commands,    NULL },
	{ 0, 0, 0, 0 }
};


/**
 * Process (SCPI) command and execute associated command function.
 *
 * This process splits command into subcommands using ':' character.
 * And tries to find the command in the command structure, finally
 * calling the command function if command is found.
 *
 * @param cmd Command string.
 * @param cmd_level pointer to command structure tree to start search.
 * @param cmd_stack data structure to store subcommands found when parsing the command.
 *
 * @return command result (SCPI error code)
 */
static const struct cmd_t* run_cmd(char *cmd, const struct cmd_t *cmd_level, struct prev_cmd_t *cmd_stack)
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
			cmd_stack->depth = 0;
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
						if (cmd_stack->depth < MAX_CMD_DEPTH)
							cmd_stack->cmds[cmd_stack->depth++] = s;
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
								cmd_stack);
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

/**
 * Process command string received from user.
 *
 * Splits command string into multiple commands by ';' character.
 * And executes each command using run_cmd() function.
 *
 * @param state Current system state.
 * @param config Current system configuration.
 * @param command command string
 */
void process_command(const struct fanpico_state *state, struct fanpico_config *config, char *command)
{
	char *saveptr, *cmd;
	struct prev_cmd_t cmd_stack;
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
			cmd_stack.depth = 0;
			cmd_stack.cmds[0] = NULL;
			cmd_level = run_cmd(cmd, cmd_level, &cmd_stack);
		}
		cmd = strtok_r(NULL, ";", &saveptr);
	}
}

/**
 * Return last command status code.
 *
 * @return status code
 */
int last_command_status()
{
	return last_error_num;
}
