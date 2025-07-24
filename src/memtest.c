/* memtest.c
   Copyright (C) 2025 Timo Kokkonen <tjko@iki.fi>

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
#include <string.h>
#include <stdint.h>
#include "pico/stdlib.h"

void* walking_mem_test(void *heap, size_t size)
{
	uint32_t len = size / sizeof(uint32_t);
	uint32_t *t = (uint32_t*)heap;
	absolute_time_t t_start, t_end;

	printf("Walking 1's test: ");
	t_start = get_absolute_time();

	for (uint32_t bit = 0; bit < 32; bit++) {
		uint32_t val = 1 << bit;

		/* Write bits */
		for (uint32_t i = 0; i < len; i++) {
			t[i] = val;
			val <<= 1;
			if (val == 0)
				val = 1;
		}

		val = 1 << bit;

		/* Read bits */
		for (uint32_t i = 0; i < len; i++) {
			if (t[i] != val) {
				printf(" ERROR: %p\n", &t[i]);
				return &t[i];
			}
			val <<= 1;
			if (val == 0)
				val = 1;
		}
		printf(".");
	}

	t_end = get_absolute_time();
	int64_t d = absolute_time_diff_us(t_start, t_end);
	int64_t speed = ((int64_t)size * 1000000) / d;
	printf(" OK (%lld KB/s)\n", speed / 1024);

	return NULL;
}


int simple_speed_mem_test(void *heap, size_t size)
{
	uint32_t *psram = (uint32_t*)heap;
	uint32_t len = size / sizeof(uint32_t);
	uint64_t start, end, speed;
	volatile uint64_t read;

	printf("Testing write speed (32bit)...");
	start = time_us_64();
	for (int i = 0; i < len; i ++) {
		psram[i] = 0xdeadbeef;
	}
	end = time_us_64();
	speed = ((uint64_t)size * 1000000) / (end - start);
	printf(" %llu KB/s\n", speed / 1024);

	printf("Testing read speed (32bit)....");
	start = time_us_64();
	for (int i = 0; i < len; i ++) {
		read = psram[i];
	}
	end = time_us_64();
	if (read != 0xdeadbeef) {
		printf(" (error)");
	}
	speed = ((uint64_t)size * 1000000) / (end - start);
	printf(" %llu KB/s\n", speed / 1024);


	return 0;
}
