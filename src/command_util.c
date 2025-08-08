/* command_util.c
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
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "cJSON.h"
#include "pico_sensor_lib.h"
#include "fanpico.h"
#ifdef WIFI_SUPPORT
#include "lwip/ip_addr.h"
#include "lwip/stats.h"
#include "pico_telnetd/util.h"
#endif


#include "command_util.h"

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
const struct cmd_t* run_cmd(char *cmd, const struct cmd_t *commands, const struct cmd_t *cmd_level, struct prev_cmd_t *cmd_stack, int *last_error_num)
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
		*last_error_num = -113;
	}
	else if (res == 0) {
		*last_error_num = 0;
	}
	else if (res == 1) {
		*last_error_num = -100;
	}
	else if (res == 2) {
		*last_error_num = -102;
	} else {
		*last_error_num = -1;
	}

	return cmd_level;
}


/**
 * Extract (unsigned) number from end of command string.
 *
 * If string is "MBFAN3" return value is 3.
 *
 * @param cmd command string
 *
 * @return number extracted from string (negative values indicates error)
 */
int get_cmd_index(const char *cmd)
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
const char* get_prev_cmd(const struct prev_cmd_t *prev_cmd, uint depth)
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
int get_prev_cmd_index(const struct prev_cmd_t *prev_cmd, uint depth)
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

int string_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
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



int bitmask16_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
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
			char *o = strdup(bitmask_to_str(old, len, base, true));
			if (o) {
				log_msg(LOG_NOTICE, "%s change '%s' --> '%s'", name, o,
					bitmask_to_str(new, len, base, true));
				free(o);
			} else {
				log_msg(LOG_NOTICE, "%s change 0x%08lx --> 0x%08lx", name, old, new);
			}
			*mask = new;
		}
		return 0;
	}
	return 1;
}

int uint32_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
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

int uint16_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
		uint16_t *var, uint16_t min_val, uint16_t max_val, const char *name)
{
	uint16_t val;
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

int uint8_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
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

int float_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
		float *var, float min_val, float max_val, const char *name)
{
	float val;

	if (query) {
		printf("%f\n", *var);
		return 0;
	}

	if (str_to_float(args, &val)) {
		if (val >= min_val && val <= max_val) {
			if (*var != val) {
				log_msg(LOG_NOTICE, "%s change %f --> %f", name, *var, val);
				*var = val;
			}
			return 0;
		}
	}
	log_msg(LOG_WARNING, "Invalid %s value: '%s'", name, args);

	return 1;
}


int bool_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
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
		log_msg(LOG_NOTICE, "%s change %s --> %s", name,
			(*var ? "ON" : "OFF"), (val ? "ON" : "OFF"));;
		*var = val;
	}
	return 0;
}

#ifdef WIFI_SUPPORT
int ip_change(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd, const char *name, ip_addr_t *ip)
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

int ip_list_change(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd, const char *name, ip_addr_t *ip, uint32_t list_len)
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


int acl_list_change(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd, const char *name, acl_entry_t *acl, uint32_t list_len)
{
	ip_addr_t tmpip;
	char buf[255], tmp[32];
	char *t, *t2, *arg, *saveptr, *saveptr2;
	int prefix;
	int idx = 0;
	int count = 0;


	if (query) {
		for (int i = 0; i < list_len; i++) {
			if (acl[i].prefix > 0) {
				if (count > 0)
					printf(", ");
				printf("%s/%u", ipaddr_ntoa(&acl[i].ip), acl[i].prefix);
				count++;
			}
		}
		printf("\n");
	} else {
		if (!(arg = strdup(args)))
			return 2;
		t = strtok_r(arg, ",", &saveptr);
		while (t && idx < list_len) {
			t2 = strtok_r(t, "/", &saveptr2);
			if (t2 && ipaddr_aton(t2, &tmpip)) {
				t2 = strtok_r(NULL, "/", &saveptr2);
				if (t2 && str_to_int(t2, &prefix, 10)) {
					ip_addr_copy(acl[idx].ip, tmpip);
					acl[idx].prefix = clamp_int(prefix,
								0, IP_IS_V6(tmpip) ? 128 : 32);
					idx++;
				}
			}

			t = strtok_r(NULL, ",", &saveptr);
		}
		free(arg);
		if (idx == 0)
			return 1;

		buf[0] = 0;
		for (int i = 0; i < list_len; i++) {
			if (i >= idx) {
				ip_addr_set_zero(&acl[i].ip);
				acl[i].prefix = 0;
			}
			if (acl[i].prefix > 0) {
				snprintf(tmp, sizeof(tmp), "%s%s/%u", (count > 0 ? ", " : ""),
					ipaddr_ntoa(&acl[i].ip), acl[i].prefix);
				strncatenate(buf, tmp, sizeof(buf));
				count++;
			}
		}
		log_msg(LOG_NOTICE, "%s changed to '%s'", name, buf);
	}
	return 0;
}
#endif



int array_string_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
			int cmd_offset, void *array, size_t array_size,
			size_t element_size, size_t var_offset, size_t var_len,
			const char *name_template, validate_str_func_t validate_func)
{
	int idx = get_prev_cmd_index(prev_cmd, cmd_offset) - 1;

	if (idx >= 0 && idx < array_size) {
		char *var = (char*)(array + (idx * element_size) + var_offset);
		char name[64];

		snprintf(name, sizeof(name), name_template, idx + 1);
		name[sizeof(name) - 1 ] = 0;

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
	return 1;
}

int array_uint8_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
			int cmd_offset, void *array, size_t array_size,
			size_t element_size, size_t var_offset,
			const char *name_template, uint8_t min_val, uint8_t max_val)
{
	int idx = get_prev_cmd_index(prev_cmd, cmd_offset) - 1;

	if (idx >= 0 && idx < array_size) {
		uint8_t *var = (uint8_t*)(array + (idx * element_size) + var_offset);
		int val;
		char name[64];

		snprintf(name, sizeof(name), name_template, idx + 1);
		name[sizeof(name) - 1 ] = 0;

		if (query) {
			printf("%u\n", *var);
		}
		else if (str_to_int(args, &val, 10)) {
			if (val < min_val || val > max_val) {
				log_msg(LOG_WARNING, "%s invalid argument: '%s'", name, args);
				return 2;
			}
			if (val != *var) {
				log_msg(LOG_NOTICE, "%s change %d --> %d", name, *var, val);
				*var = val;
			}
			return 0;
		}
	}
	return 1;
}

int array_uint16_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
			int cmd_offset, void *array, size_t array_size,
			size_t element_size, size_t var_offset,
			const char *name_template, uint16_t min_val, uint16_t max_val)
{
	int idx = get_prev_cmd_index(prev_cmd, cmd_offset) - 1;

	if (idx >= 0 && idx < array_size) {
		uint16_t *var = (uint16_t*)(array + (idx * element_size) + var_offset);
		int val;
		char name[64];

		snprintf(name, sizeof(name), name_template, idx + 1);
		name[sizeof(name) - 1 ] = 0;

		if (query) {
			printf("%u\n", *var);
		}
		else if (str_to_int(args, &val, 10)) {
			if (val < min_val || val > max_val) {
				log_msg(LOG_WARNING, "%s invalid argument: '%s'", name, args);
				return 2;
			}
			if (val != *var) {
				log_msg(LOG_NOTICE, "%s change %d --> %d", name, *var, val);
				*var = val;
			}
			return 0;
		}
	}
	return 1;
}

int array_float_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
			int cmd_offset, void *array, size_t array_size,
			size_t element_size, size_t var_offset,
			const char *name_template, float min_val, float max_val)
{
	int idx = get_prev_cmd_index(prev_cmd, cmd_offset) - 1;

	if (idx >= 0 && idx < array_size) {
		float *var = (float*)(array + (idx * element_size) + var_offset);
		float val;
		char name[64];

		snprintf(name, sizeof(name), name_template, idx + 1);
		name[sizeof(name) - 1 ] = 0;

		if (query) {
			printf("%f\n", *var);
		}
		else if (str_to_float(args, &val)) {
			if (val < min_val || val > max_val) {
				log_msg(LOG_WARNING, "%s invalid argument: '%s'", name, args);
				return 2;
			}
			if (val != *var) {
				log_msg(LOG_NOTICE, "%s change %f --> %f", name, *var, val);
				*var = val;
			}
			return 0;
		}
	}
	return 1;
}

