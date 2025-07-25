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
	volatile uint32_t *t = (uint32_t*)heap;
	uint32_t len = size / sizeof(uint32_t);
	uint64_t t_start, t_end, speed;

	printf("Walking 1's test: ");

	t_start = time_us_64();
	for (uint32_t bit = 0; bit < 32; bit++) {
		/* Write bits */
		for (uint32_t i = 0; i < len; i++)
			t[i] = 1 << ((bit + i) % 32);

		/* Read bits */
		for (uint32_t i = 0; i < len; i++)
			if (t[i] != 1 << ((bit + i) % 32)) {
				printf(" ERROR: %p (%lu)\n", &t[i], i);
				return (void*)&t[i];
			}
		printf(".");
	}
	t_end = time_us_64();

	speed = ((uint64_t)size * 1000000) / (t_end - t_start);
	printf(" OK (%lld KB/s)\n", speed / 1024);

	return NULL;
}

int simple_speed_mem_test(void *heap, size_t size, bool readonly)
{
	volatile uint32_t *t = (uint32_t*)heap;
	uint32_t len = size / sizeof(uint32_t);
	uint64_t start, end, speed;
	volatile uint64_t read;

	if (!readonly) {
		printf("Testing write speed (32bit)...");
		start = time_us_64();
		for (int i = 0; i < len; i ++) {
			t[i] = 0xdeadbeef;
		}
		end = time_us_64();
		speed = ((uint64_t)size * 1000000) / (end - start);
		printf(" %llu KB/s\n", speed / 1024);
	}

	printf("Testing read speed (32bit)....");
	start = time_us_64();
	for (int i = 0; i < len; i ++) {
		read = t[i];
	}
	end = time_us_64();
	if (!readonly && read != 0xdeadbeef) {
		printf(" (error)");
	}
	speed = ((uint64_t)size * 1000000) / (end - start);
	printf(" %llu KB/s\n", speed / 1024);

	if (!readonly) {
		printf("memset() speed,...............");
		start = time_us_64();
		memset(heap, 0, size);
		end = time_us_64();
		speed = ((uint64_t)size * 1000000) / (end - start);
		printf(" %llu KB/s\n", speed / 1024);
	}

	return 0;
}
