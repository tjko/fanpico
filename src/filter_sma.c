/* filter_sma.c
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


#define SMA_WINDOW_MAX_SIZE 32

typedef struct sma_context {
	float data[SMA_WINDOW_MAX_SIZE];
	double sum;
	uint8_t index;
	uint8_t used;
	uint8_t window;
} sma_context_t;


void* sma_parse_args(char *args)
{
	sma_context_t *c;
	char *tok, *saveptr;
	int window, i;

	if (!args)
		return NULL;

	/* window parameter (samples) */
	if (!(tok = strtok_r(args, ",", &saveptr)))
		return NULL;
	if (!str_to_int(tok, &window, 10))
		return NULL;
	if (window < 2 || window > SMA_WINDOW_MAX_SIZE)
		return NULL;

	if (!(c = malloc(sizeof(sma_context_t))))
		return NULL;

	c->index = 0;
	c->used = 0;
	c->window = window;
	c->sum = 0.0;
	for(i = 0; i < SMA_WINDOW_MAX_SIZE; i++)
		c->data[i] = 0.0;

	return c;
}

char* sma_print_args(void *ctx)
{
	sma_context_t *c = (sma_context_t*)ctx;
	char buf[128];

	snprintf(buf, sizeof(buf), "%u", c->window);

	return strdup(buf);
}

float sma_filter(void *ctx, float input)
{
	sma_context_t *c = (sma_context_t*)ctx;
	float output;

	if (c->used < c->window) {
		/* Ring buffer not yet full */
		c->used++;
	} else {
		/* Ring buffer is full, discard oldest value. */
		c->sum -= c->data[c->index];
	}
	c->data[c->index] = input;
	c->sum += input;
	output = c->sum / c->used;

	//printf("sma: index=%u,window=%u,used=%u,sum=%lf,input=%f,return=%f\n",
	// 	c->index, c->window, c->used, c->sum, input, output);

	/* Point index to next slot in the ring buffer. */
	c->index = ((c->index + 1) % c->window);

	return output;
}


/* eof :-) */
