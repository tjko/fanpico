/* util_rp2040.c
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

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "pico/unique_id.h"
#include "pico/multicore.h"
#include "hardware/watchdog.h"

#include "pico_lfs.h"

#include "fanpico.h"


extern char __StackTop;
extern char __StackOneTop;
extern char __StackBottom;
extern char __StackOneBottom;
extern char __StackLimit;
extern char __HeapLimit;
extern char __end__;
extern char __flash_binary_start;
extern char __flash_binary_end;


inline uint32_t get_stack_pointer() {
	uint32_t sp;

	asm volatile("mov %0, sp" : "=r"(sp));

	return sp;
}


inline uint32_t get_stack_free()
{
	uint32_t sp = get_stack_pointer();
	uint32_t end = get_core_num() ? (uint32_t)&__StackOneBottom : (uint32_t)&__StackBottom;

	return (sp > end ? sp - end : 0);
}

void print_rp2_meminfo()
{
	printf("Core0 stack size:                      %d\n",
		&__StackTop - &__StackBottom);
	printf("Core1 stack size:                      %d\n",
		&__StackOneTop - &__StackOneBottom);
	printf("Heap size:                             %d\n",
		&__StackLimit - &__end__);
	printf("Core%d stack free:                      %lu\n",
		get_core_num(), get_stack_free());
}

#if PICO_SDK_VERSION_MAJOR < 2
void watchdog_disable()
{
	hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
}
#endif

void print_irqinfo()
{
	uint core = get_core_num();
	uint enabled, shared;

	for(uint i = 0; i < 32; i++) {
		enabled = irq_is_enabled(i);
		shared = (enabled ? irq_has_shared_handler(i) : 0);
		printf("core%u: IRQ%-2u: enabled=%u, shared=%u\n", core, i, enabled, shared);
	}
}


const char *rp2_model_str()
{
	static char buf[32];
	uint8_t version = 0;
	uint8_t known_chip = 0;
	uint8_t chip_version = 0;
	char *model;
	char r;

#if PICO_RP2350
#if PICO_RP2350A
	model = "2350A";
#else
	model = "2350B";
#endif
	r = 'A';
	chip_version = rp2350_chip_version();
	if (chip_version <= 2)
		known_chip = 1;
	version = chip_version;
#else
	model = "2040";
	r = 'B';
	uint8_t rom_version = rp2040_rom_version();
	chip_version = rp2040_chip_version();
	if (chip_version <= 2 && rom_version <= 3)
		known_chip = 1;
	version = rom_version - 1;
#endif

	snprintf(buf, sizeof(buf), "RP%s-%c%d%s",
		model, r, version,
		(known_chip ? "" : " (?)"));

	return buf;
}


const char *pico_serial_str()
{
	static char buf[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
	pico_unique_board_id_t board_id;

	memset(&board_id, 0, sizeof(board_id));
	pico_get_unique_board_id(&board_id);
	for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++)
		snprintf(&buf[i*2], 3,"%02x", board_id.id[i]);

	return buf;
}


int time_passed(absolute_time_t *t, uint32_t ms)
{
	absolute_time_t t_now = get_absolute_time();

	if (t == NULL)
		return -1;

	if (to_us_since_boot(*t) == 0 ||
	    to_us_since_boot(delayed_by_ms(*t, ms)) < to_us_since_boot(t_now)) {
		*t = t_now;
		return 1;
	}

	return 0;
}


int time_elapsed(absolute_time_t t, uint32_t ms)
{
	absolute_time_t t_now = get_absolute_time();

	if (absolute_time_diff_us(t, t_now) > (uint64_t)ms * 1000)
		return 1;

	return 0;
}


int getstring_timeout_ms(char *str, uint32_t maxlen, uint32_t timeout)
{
	absolute_time_t t_timeout = get_absolute_time();
	char *p;
	int res = 0;
	int len;

	if (!str || maxlen < 2)
		return -1;

	len = strnlen(str, maxlen);
	if (len >= maxlen)
		return -2;
	p = str + len;

	while ((p - str) < (maxlen - 1) ) {
		if (time_passed(&t_timeout, timeout)) {
			break;
		}
		int c = getchar_timeout_us(1);
		if (c == PICO_ERROR_TIMEOUT)
			continue;
		if (c == 10 || c == 13) {
			res = 1;
			break;
		}
		*p++ = c;
	}
	*p = 0;

	return res;
}



/* eof */
