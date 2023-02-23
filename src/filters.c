/* filters.c
   Copyright (C) 2023 Timo Kokkonen <tjko@iki.fi>

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
#include <math.h>
#include <assert.h>
#include "pico/stdlib.h"

#include "fanpico.h"
#include "filters.h"


filter_entry_t filters[] = {
	{ "none", NULL, NULL, NULL }, /* FILTER_NONE */
	{ "lossypeak", lossy_peak_parse_args, lossy_peak_print_args, lossy_peak_filter }, /* FILTER_LOSSYPEAK */
	{ "sma", sma_parse_args, sma_print_args, sma_filter }, /* FILTER_SMA */
	{ NULL, NULL, NULL, NULL }
};


int str2filter(const char *s)
{
	int ret = FILTER_NONE;

	for(int i = 0; filters[i].name; i++) {
		if (!strcasecmp(s, filters[i].name)) {
			ret = i;
			break;
		}
	}

	return ret;
}


const char* filter2str(enum pwm_filter_types source)
{
	for (int i = 0; filters[i].name; i++) {
		if (source == i) {
			return filters[i].name;
		}
	}

	return "none";
}


void* filter_parse_args(enum pwm_filter_types filter, char *args)
{
	void *ret = NULL;

	if (filter <= FILTER_ENUM_MAX) {
		if (filters[filter].parse_args_func)
			ret = filters[filter].parse_args_func(args);
	}

	return ret;
}


char* filter_print_args(enum pwm_filter_types filter, void *ctx)
{
	char *ret = NULL;

	if (filter <= FILTER_ENUM_MAX) {
		if (filters[filter].print_args_func)
			ret = filters[filter].print_args_func(ctx);
	}

	return ret;
}


float filter(enum pwm_filter_types filter, void *ctx, float input)
{
	float ret = input;

	if (filter <= FILTER_ENUM_MAX) {
		if (filters[filter].filter_func)
			ret = filters[filter].filter_func(ctx, input);
	}

	return ret;
}


/* eof :-) */
