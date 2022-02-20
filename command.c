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
	int (*func)(const char *cmd, const char *args);
};


int cmd_idn(const char *cmd, const char *args)
{
	int i;
	pico_unique_board_id_t board_id;

	printf("TJKO Industries,FANPICO-0408,");
	pico_get_unique_board_id(&board_id);
	for (i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++)
		printf("%02x", board_id.id[i]);
	printf(",%s\n", FANPICO_VERSION);

	return 0;
}


int cmd_null(const char *cmd, const char *args)
{
	debug(1, "null command: %s %s\n", cmd, args);
	return 0;
}

int cmd_set_debug(const char *cmd, const char *args)
{
	int level = atoi(args);

	set_debug_level((level < 0 ? 0 : level));
	return 0;
}

int cmd_get_debug(const char *cmd, const char *args)
{
	printf("%d\n", get_debug_level());
	return 0;
}

int cmd_reset(const char *cmd, const char *args)
{
	debug(1, "Initiating reboot...\n");
	watchdog_enable(10, 0);
	while (1);
}

int cmd_save_config(const char *cmd, const char *args)
{
	save_config();
	return 0;
}

int cmd_print_config(const char *cmd, const char *args)
{
	print_config();
	return 0;
}

int cmd_delete_config(const char *cmd, const char *args)
{
	delete_config();
	return 0;
}



struct cmd_t system_commands[] = {
	{ "MODe", 3,  NULL, cmd_null },
	{ "DEBUG?",6, NULL, cmd_get_debug },
	{ "DEBUG", 5, NULL, cmd_set_debug },
	{ 0, 0, 0, 0 }
};

struct cmd_t config_commands[] = {
	{ "SAVe", 3, NULL, cmd_save_config },
	{ "PRInt", 3, NULL, cmd_print_config },
	{ "DELete", 3, NULL, cmd_delete_config },
};

struct cmd_t commands[] = {
	{ "*IDN?", 5, NULL, cmd_idn },
	{ "*CLS", 4, NULL, cmd_null },
	{ "*RST", 4, NULL, cmd_reset },
	{ "SYStem", 3, system_commands, NULL },
	{ "CONFig", 4, config_commands, NULL },
	{ 0, 0, 0, 0 }
};


char *trim_str(char *s)
{
	char *e;

	if (!s) return NULL;

	while (iswspace(*s))
		s++;
	e = s + strlen(s) - 1;
	while (e >= s && iswspace(*e))
		*e-- = 0;

	return s;
}



struct cmd_t* run_cmd(struct fanpico_state *state, struct fanpico_config *config,
		char *cmd, struct cmd_t *cmd_level)
{
	int i;
	int res = -1;
	char *saveptr1, *saveptr2, *t, *sub, *s, *arg;

	t = strtok_r(cmd, " \t", &saveptr1);
	if (t && strlen(t) > 0) {
		if (*t == ':' || *t == '*') {
			/* reset command level to 'root' */
			cmd_level = commands;
		}
		/* Split command to subcommands and search from command tree ... */
		sub = strtok_r(t, ":", &saveptr2);
		while (sub) {
			s = sub;
			sub = NULL;
			i = 0;
			while (cmd_level[i].cmd) {
				if (!strncasecmp(s, cmd_level[i].cmd, cmd_level[i].min_match)) {
					if (cmd_level[i].subcmds) {
						/* Match for subcommand...*/
						sub = strtok_r(NULL, ":", &saveptr2);
						cmd_level = cmd_level[i].subcmds;
					} else {
						/* Match for command */
						arg = strtok_r(NULL, " \t", &saveptr1);
						res = cmd_level[i].func(s, (arg ? arg : ""));
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

	cmd = strtok_r(command, ";", &saveptr);
	while (cmd) {
		cmd = trim_str(cmd);
		debug(1, "command: '%s'\n", cmd);
		if (cmd && strlen(cmd) > 0) {
			cmd_level = run_cmd(state, config, cmd, cmd_level);
		}
		cmd = strtok_r(NULL, ";", &saveptr);
	}
}
