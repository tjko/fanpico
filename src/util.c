/* util.c
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
#include <stdarg.h>
#include <string.h>
#include <wctype.h>
#include <assert.h>
#include <malloc.h>
#include "pico/stdlib.h"

#include "fanpico.h"


int global_debug_level = 0;


int get_debug_level()
{
	return global_debug_level;
}


void set_debug_level(int level)
{
	global_debug_level = level;
}


void debug(int debug_level, const char *fmt, ...)
{
	va_list ap;

	if (debug_level > global_debug_level)
		return;

	printf("[DEBUG] ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}


void print_mallinfo()
{
	printf("mallinfo:\n");
	printf("    arena: %u\n", mallinfo().arena);
	printf("  ordblks: %u\n", mallinfo().ordblks);
	printf(" uordblks: %u\n", mallinfo().uordblks);
	printf(" fordblks: %u\n", mallinfo().fordblks);
}


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


const char *rp2040_model_str()
{
	static char buf[32];
	uint8_t version = 0;
	uint8_t known_chip = 0;
	uint8_t chip_version = rp2040_chip_version();
	uint8_t rom_version = rp2040_rom_version();


	if (chip_version <= 2 && rom_version <= 3)
		known_chip = 1;

	version = rom_version - 1;

	snprintf(buf, sizeof(buf), "RP2040-B%d%s",
		version,
		(known_chip ? "" : " (?)"));

	return buf;
}


