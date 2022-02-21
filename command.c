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
#include <string.h>
#include <wctype.h>
#include <assert.h>
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "hardware/watchdog.h"

#include "fanpico.h"


struct cmd_t {
	const char   *cmd;
	uint8_t       min_match;
	struct cmd_t *subcmds;
	int (*func)(const char *cmd, const char *args, int query, char *prev_cmd);
};

struct fanpico_state *st = NULL;
struct fanpico_config *conf = NULL;


int cmd_idn(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int i;
	pico_unique_board_id_t board_id;

	if (query) {
		printf("TJKO Industries,FANPICO-%s,", FANPICO_MODEL);
		pico_get_unique_board_id(&board_id);
		for (i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++)
			printf("%02x", board_id.id[i]);
		printf(",%s\n", FANPICO_VERSION);
	}

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
	if (!query)
		return 0;



	return 0;
}

int cmd_fan_name(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;

	if (!prev_cmd)
		return 0;

	fan = atoi(&prev_cmd[3]) - 1;
	if (fan >= 0 && fan < FAN_MAX_COUNT) {
		if (query) {
			printf("%s\n", conf->fans[fan].name);
		} else {
			debug(1, "fan%d: change name '%s' --> '%s'\n", fan + 1,
				conf->fans[fan].name, args);
			strncpy(conf->fans[fan].name, args, sizeof(conf->fans[fan].name));
		}
	}
	return 0;
}

int cmd_fan_rpm(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	double rpm;

	if (query && prev_cmd) {
		fan = atoi(&prev_cmd[3]) - 1;
		if (fan >= 0 && fan < FAN_MAX_COUNT) {
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

	if (query && prev_cmd) {
		fan = atoi(&prev_cmd[3]) - 1;
		if (fan >= 0 && fan < FAN_MAX_COUNT) {
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

	if (query && prev_cmd) {
		fan = atoi(&prev_cmd[3]) - 1;
		if (fan >= 0 && fan < FAN_MAX_COUNT) {
			d = st->fan_duty[fan];
			debug(2,"fan%d duty = %f%%\n", fan + 1, d);
			printf("%.0f\n", d);
		}
	}
	return 0;
}

int cmd_mbfan_name(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int mbfan;

	if (!prev_cmd)
		return 0;

	mbfan = atoi(&prev_cmd[5]) - 1;
	if (mbfan >= 0 && mbfan < MBFAN_MAX_COUNT) {
		if (query) {
			printf("%s\n", conf->mbfans[mbfan].name);
		} else {
			debug(1, "mbfan%d: change name '%s' --> '%s'\n", mbfan + 1,
				conf->mbfans[mbfan].name, args);
			strncpy(conf->mbfans[mbfan].name, args, sizeof(conf->mbfans[mbfan].name));
		}
	}
	return 0;
}

int cmd_mbfan_rpm(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int fan;
	double rpm;

	if (query && prev_cmd) {
		fan = atoi(&prev_cmd[5]) - 1;
		if (fan >= 0 && fan < MBFAN_MAX_COUNT) {
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

	if (query && prev_cmd) {
		fan = atoi(&prev_cmd[5]) - 1;
		if (fan >= 0 && fan < MBFAN_MAX_COUNT) {
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

	if (query && prev_cmd) {
		fan = atoi(&prev_cmd[5]) - 1;
		if (fan >= 0 && fan < MBFAN_MAX_COUNT) {
			d = st->mbfan_duty[fan];
			debug(2,"mbfan%d duty = %f%%\n", fan + 1, d);
			printf("%.0f\n", d);
		}
	}
	return 0;
}

int cmd_sensor_temp(const char *cmd, const char *args, int query, char *prev_cmd)
{
	int sensor;
	float d;

	if (query && prev_cmd) {
		sensor = atoi(&prev_cmd[6]) - 1;
		if (sensor >= 0 && sensor < SENSOR_MAX_COUNT) {
			d = st->temp[sensor];
			debug(2,"sensor%d temp = %fC\n", sensor + 1, d);
			printf("%.0f\n", d);
		}
	}
	return 0;
}



struct cmd_t system_commands[] = {
	{ "DEBUG",  5, NULL,              cmd_debug },
	{ 0, 0, 0, 0 }
};

struct cmd_t fan_c_commands[] = {
	{ "NAME",    4, NULL,             cmd_fan_name },
};

struct cmd_t mbfan_c_commands[] = {
	{ "NAME",    4, NULL,             cmd_mbfan_name },
};

struct cmd_t config_commands[] = {
	{ "SAVe",    3, NULL,             cmd_save_config },
	{ "Read",    1, NULL,             cmd_print_config },
	{ "DELete",  3, NULL,             cmd_delete_config },
	{ "FAN",     3, fan_c_commands,   NULL },
	{ "MBFAN",   5, mbfan_c_commands, NULL },
	{ 0, 0, 0, 0}
};

struct cmd_t fan_commands[] = {
	{ "RPM",     3, NULL,             cmd_fan_rpm },
	{ "PWM",     3, NULL,             cmd_fan_pwm },
	{ "TACho",   3, NULL,             cmd_fan_tacho },
};

struct cmd_t mbfan_commands[] = {
	{ "RPM",     3, NULL,             cmd_mbfan_rpm },
	{ "PWM",     3, NULL,             cmd_mbfan_pwm },
	{ "TACho",   3, NULL,             cmd_mbfan_tacho },
};

struct cmd_t sensor_commands[] = {
	{ "TEMP",   4, NULL,              cmd_sensor_temp },
};

struct cmd_t measure_commands[] = {
	{ "FAN",     3, fan_commands,     NULL },
	{ "MBFAN",   5, mbfan_commands,   NULL },
	{ "SENSOR",  6, sensor_commands,  NULL },
	{ 0, 0, 0, 0}
};

struct cmd_t commands[] = {
	{ "*CLS",    4, NULL,             cmd_null },
	{ "*ESE",    4, NULL,             cmd_null },
	{ "*ESR",    4, NULL,             cmd_zero },
	{ "*IDN",    4, NULL,             cmd_idn },
	{ "*OPC",    4, NULL,             cmd_one },
	{ "*RST",    4, NULL,             cmd_reset },
	{ "*SRE",    4, NULL,             cmd_zero },
	{ "*STB",    4, NULL,             cmd_zero },
	{ "*TST",    4, NULL,             cmd_zero },
	{ "*WAI",    4, NULL,             cmd_null },
	{ "CONFig",  4, config_commands,  NULL },
	{ "MEAsure", 3, measure_commands, NULL },
	{ "SYStem",  3, system_commands,  NULL },
	{ "Read",    1, NULL,             cmd_read },
	{ 0, 0, 0, 0 }
};



struct cmd_t* run_cmd(char *cmd, struct cmd_t *cmd_level)
{
	int i, query, cmd_len;
	int res = -1;
	char *prev_subcmd = NULL;
	char *saveptr1, *saveptr2, *t, *sub, *s, *arg;

	t = strtok_r(cmd, " \t", &saveptr1);
	if (t && strlen(t) > 0) {
		cmd_len = strlen(t);
		if (*t == ':' || *t == '*') {
			/* reset command level to 'root' */
			cmd_level = commands;
		}
		/* Split command to subcommands and search from command tree ... */
		sub = strtok_r(t, ":", &saveptr2);
		while (sub && strlen(sub) > 0) {
			s = sub;
			sub = NULL;
			i = 0;
			while (cmd_level[i].cmd) {
				if (!strncasecmp(s, cmd_level[i].cmd, cmd_level[i].min_match)) {
					if (cmd_level[i].subcmds) {
						/* Match for subcommand...*/
						prev_subcmd = s;
						sub = strtok_r(NULL, ":", &saveptr2);
						cmd_level = cmd_level[i].subcmds;
					} else {
						/* Match for command */
						query = (s[strlen(s)-1] == '?' ? 1 : 0);
						arg = t + cmd_len + 1;
						res = cmd_level[i].func(s,
									(arg ? arg : ""),
									query,
									prev_subcmd);
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
			cmd_level = run_cmd(cmd, cmd_level);
		}
		cmd = strtok_r(NULL, ";", &saveptr);
	}
}
