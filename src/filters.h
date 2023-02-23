/* filters.h
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

#ifndef FANPICO_FILTERS_H
#define FANPICO_FILTERS_H 1

typedef void* (filter_parse_args_func_t)(char *args);
typedef char* (filter_print_args_func_t)(void *ctx);
typedef float (filter_func_t)(void *ctx, float input);

typedef struct filter_entry {
	const char* name;
	filter_parse_args_func_t *parse_args_func;
	filter_print_args_func_t *print_args_func;
	filter_func_t *filter_func;
} filter_entry_t;

/* filters_lossypeak.c */
void* lossy_peak_parse_args(char *args);
char* lossy_peak_print_args(void *ctx);
float lossy_peak_filter(void *ctx, float input);

/* filters_sma.c */
void* sma_parse_args(char *args);
char* sma_print_args(void *ctx);
float sma_filter(void *ctx, float input);


#endif /* FANPICO_FILTERS_H */
