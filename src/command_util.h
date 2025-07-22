/* command_util.h
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

#ifndef FANPICO_COMMAND_UTIL_H
#define FANPICO_COMMAND_UTIL_H 1

#ifdef WIFI_SUPPORT
#include "lwip/ip_addr.h"
#endif

#define MAX_CMD_DEPTH 16

typedef int (*validate_str_func_t)(const char *args);

struct prev_cmd_t {
	uint depth;
	char* cmds[MAX_CMD_DEPTH];
};

struct cmd_t {
	const char   *cmd;
	uint8_t       min_match;
	const struct cmd_t *subcmds;
	int (*func)(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd);
};


/* command_util.h */

const struct cmd_t* run_cmd(char *cmd, const struct cmd_t *commands,
			const struct cmd_t *cmd_level, struct prev_cmd_t *cmd_stack,
			int *last_error_num);
int get_cmd_index(const char *cmd);
const char* get_prev_cmd(const struct prev_cmd_t *prev_cmd, uint depth);
int get_prev_cmd_index(const struct prev_cmd_t *prev_cmd, uint depth);
int string_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
		char *var, size_t var_len, const char *name, validate_str_func_t validate_func);
int bitmask16_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
		uint16_t *mask, uint16_t len, uint8_t base, const char *name);
int float_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
		float *var, float min_val, float max_val, const char *name);
int uint32_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
		uint32_t *var, uint32_t min_val, uint32_t max_val, const char *name);
int uint8_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
		uint8_t *var, uint8_t min_val, uint8_t max_val, const char *name);
int bool_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
		bool *var, const char *name);
#ifdef WIFI_SUPPORT
int ip_change(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
		const char *name, ip_addr_t *ip);
int ip_list_change(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
		const char *name, ip_addr_t *ip, uint32_t list_len);
#endif
int array_string_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
			int cmd_offset, void *array, size_t array_size,
			size_t element_size, size_t var_offset, size_t var_len,
			const char *name, validate_str_func_t validate_func);
int array_uint8_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
			int cmd_offset, void *array, size_t array_size,
			size_t element_size, size_t var_offset,
			const char *name_template, uint8_t min_val, uint8_t max_val);
int array_uint16_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
			int cmd_offset, void *array, size_t array_size,
			size_t element_size, size_t var_offset,
			const char *name_template, uint16_t min_val, uint16_t max_val);
int array_float_setting(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd,
			int cmd_offset, void *array, size_t array_size,
			size_t element_size, size_t var_offset,
			const char *name_template, float min_val, float max_val);



/* command.c */
int cmd_board(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd);
int cmd_version(const char *cmd, const char *args, int query, struct prev_cmd_t *prev_cmd);


#endif /* FANPICO_COMMAND_UTIL_H */
