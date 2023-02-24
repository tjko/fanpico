/* filter_lossypeak.c
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


typedef struct lossypeak_context {
	float peak;
	int64_t delay_us;
	float decay;
	absolute_time_t last_t;
	absolute_time_t peak_t;
	uint8_t state;
} lossypeak_context_t;


void* lossy_peak_parse_args(char *args)
{
	lossypeak_context_t *c;
	char *tok, *saveptr;
	float decay, delay;

	if (!args)
		return NULL;

	/* decay parameter (points per second) */
	if (!(tok = strtok_r(args, ",", &saveptr)))
		return NULL;
	if (!str_to_float(tok, &decay))
		return NULL;
	if (decay < 0.0)
		return NULL;

	/* delay parameter (seconds) */
	if (!(tok = strtok_r(NULL, ",", &saveptr)))
		return NULL;
	if (!str_to_float(tok, &delay))
		return NULL;
	if (delay < 0.0)
		return NULL;


	if (!(c = malloc(sizeof(lossypeak_context_t))))
		return NULL;

	c->peak = 0.0;
	c->delay_us = delay * 1000000;
	c->decay = decay;
	c->last_t = get_absolute_time();
	c->peak_t = 0;
	c->state = 0;

	return c;
}

char* lossy_peak_print_args(void *ctx)
{
	lossypeak_context_t *c = (lossypeak_context_t*)ctx;
	char buf[128];

	snprintf(buf, sizeof(buf), "%f,%f", c->decay, (float)(c->delay_us / 1000000.0));

	return strdup(buf);
}

float lossy_peak_filter(void *ctx, float input)
{
	lossypeak_context_t *c = (lossypeak_context_t*)ctx;
	absolute_time_t t_now = get_absolute_time();
	int64_t t_d = absolute_time_diff_us(c->last_t, t_now);

	if (input >= c->peak) {
		c->peak = input;
		c->state = 0;
		c->peak_t = t_now;
	} else {
		if (c->state == 0) {
			if (c->delay_us > 0) {
				t_d = absolute_time_diff_us(c->peak_t, t_now);
				if (t_d > c->delay_us) {
					c->state = 1;
					t_d -= c->delay_us;
				}
			} else {
				c->state = 1;
			}
		}
		if (c->state == 1) {
			float decay = (t_d / 1000000.0) * c->decay;
			if (input > c->peak - decay) {
				c->peak = input;
			} else {
				c->peak -= decay;
			}
		}
	}
	c->last_t = t_now;

	return c->peak;
}


/* eof :-) */
