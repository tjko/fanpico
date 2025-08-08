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
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#if PICO_RP2040
#include "hardware/structs/vreg_and_chip_reset.h"
#include "hardware/structs/ssi.h"
#else
#include "hardware/structs/powman.h"
#include "hardware/structs/qmi.h"
#include "psram.h"
#endif
#include "memtest.h"

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


float get_rp2_dvdd()
{
	uint8_t vsel = 0;
	float v = 0.0;

#if PICO_RP2040
	vsel = (vreg_and_chip_reset_hw->vreg & VREG_AND_CHIP_RESET_VREG_VSEL_BITS)
		>> VREG_AND_CHIP_RESET_VREG_VSEL_LSB;
	v = 0.80;
	if (vsel > 5)
		v += (vsel - 5) * 0.05;
#else
	vsel = (powman_hw->vreg & POWMAN_VREG_VSEL_BITS) >> POWMAN_VREG_VSEL_LSB;
	if (vsel <= 17)
		v = 0.55 + vsel * 0.05;
	else if (vsel <= 19)
		v = 1.50 + (vsel - 18) * 0.10;
	else if (vsel == 20)
		v = 1.65;
	else if (vsel <= 24)
		v = 1.70 + (vsel - 21) * 0.10;
	else if (vsel <= 28)
		v = 2.35 + (vsel - 25) * 0.15;
	else
		v = 3.00 + (vsel - 29) * 0.15;
#endif

	return v;
}


void print_rp2_board_info()
{
	uint32_t sys_clk = clock_get_hz(clk_sys);
#if PICO_RP2040
	uint8_t flash_clkdiv = (ssi_hw->baudr & SSI_BAUDR_SCKDV_BITS) >> SSI_BAUDR_SCKDV_LSB;
#else
	uint8_t flash_clkdiv = (qmi_hw->m[0].timing & QMI_M0_TIMING_CLKDIV_BITS)
		>> QMI_M0_TIMING_CLKDIV_LSB;
	uint8_t psram_clkdiv = (qmi_hw->m[1].timing & QMI_M1_TIMING_CLKDIV_BITS)
		>> QMI_M1_TIMING_CLKDIV_LSB;
	uint32_t psram_clk = sys_clk / psram_clkdiv;
#endif
	uint32_t flash_clk = sys_clk / flash_clkdiv;


	printf("Hardware Model: FANPICO-%s\n", FANPICO_MODEL);
	printf("         Board: %s\n", PICO_BOARD);
	printf("           MCU: %s @ %luMHz\n",	rp2_model_str(), sys_clk / 1000000);
	printf("           RAM: %luKB\n", ((uint32_t)SRAM_END - SRAM_BASE) >> 10);
#if !PICO_RP2040
	if (psram_size() > 0)
		printf("         PSRAM: %uKB @ %luMHz\n", psram_size() >> 10,
			psram_clk / 1000000);
#endif
	printf("         Flash: %luKB @ %luMHz\n",
		(uint32_t)PICO_FLASH_SIZE_BYTES >> 10, flash_clk / 1000000);
	printf(" Serial Number: %s\n", pico_serial_str());

	if (get_log_level() >= LOG_INFO)
		printf("          DVDD: %0.2fV\n", get_rp2_dvdd());
}


void print_psram_info()
{
#if PICO_RP2040 || PSRAM_CS_PIN < 0
	printf("No PSRAM support.\n");
#else
	uint8_t psram_clkdiv = (qmi_hw->m[1].timing & QMI_M1_TIMING_CLKDIV_BITS)
		>> QMI_M1_TIMING_CLKDIV_LSB;
	uint32_t psram_clk = clock_get_hz(clk_sys) / psram_clkdiv;
	const psram_id_t *p = psram_get_id();

	if (p) {
		printf("Manufacturer: %s\n", psram_get_manufacturer(p->mfid));
		printf("     Chip ID: %02x%02x%02x%02x%02x%02x%02x%02x\n", p->mfid, p->kgd,
			p->eid[0], p->eid[1], p->eid[2], p->eid[3], p->eid[4], p->eid[5]);
	}
	printf("        Size: %u KB\n", psram_size() >> 10);
	printf("       Clock: %lu MHz\n", psram_clk / 1000000);
	printf("   M1_TIMING: %08lx\n", qmi_hw->m[1].timing);
#endif
}


void rp2_memtest()
{
	void *heap;
	uint32_t size;

#if !PICO_RP2040
	/* PSRAM Tests */
	if ((size = psram_size()) > 0) {
		printf("Testing PSRAM: %lu bytes\n", size);
		if (get_log_level() >= LOG_INFO) {
			printf("M1_TIMING: %08lx\n", qmi_hw->m[1].timing);
			printf("NOCACHE:\n");
		}
		heap = (void*)PSRAM_NOCACHE_BASE;
		walking_mem_test(heap, size);
		simple_speed_mem_test(heap, size, false);
		if (get_log_level() >= LOG_INFO) {
			printf("CACHE:\n");
			heap = (void*)PSRAM_BASE;
			walking_mem_test(heap, size);
			simple_speed_mem_test(heap, size, false);
		}
	}
#endif

	/* Flash Tests */
	size = PICO_FLASH_SIZE_BYTES;
	printf("Testing FLASH: %lu bytes\n", size);
	if (get_log_level() >= LOG_INFO) {
#if PICO_RP2040
		printf("BAUDR: %08lx\n", ssi_hw->baudr);
#else
		printf("M0_TIMING: %08lx\n", qmi_hw->m[0].timing);
#endif
		printf("NOCACHE:\n");
	}
	heap = (void*)XIP_NOCACHE_NOALLOC_BASE;
	simple_speed_mem_test(heap, size, true);
	if (get_log_level() >= LOG_INFO) {
		printf("CACHE:\n");
		heap = (void*)XIP_BASE;
		simple_speed_mem_test(heap, size, true);
	}

}


#if PICO_RP2350
static inline void powman_vreg_update_in_progress_wait()
{
	while (powman_hw->vreg & POWMAN_VREG_UPDATE_IN_PROGRESS_BITS) {
		tight_loop_contents();
	}
}
#endif

void rp2_set_sys_clock(uint32_t khz)
{
#if PICO_RP2350
	if (khz > 150000) {
		/* Increase core voltage from 1.10V to 1.15V if overclocking */
		uint8_t vsel = 0x0c; // 01100 = 1.15V
		uint8_t hiz = (powman_hw->vreg & POWMAN_VREG_HIZ_BITS) >> POWMAN_VREG_HIZ_LSB;

		/* Unlock (enable) VREG control */
		hw_set_bits(&powman_hw->vreg_ctrl,
			POWMAN_PASSWORD_BITS | POWMAN_VREG_CTRL_UNLOCK_BITS);

		/* Update VSEL (select output voltage) */
		powman_vreg_update_in_progress_wait();
		powman_hw->vreg = (
			POWMAN_PASSWORD_BITS |
			((vsel << POWMAN_VREG_VSEL_LSB) & POWMAN_VREG_VSEL_BITS) |
			((hiz << POWMAN_VREG_HIZ_LSB) & POWMAN_VREG_HIZ_LSB)
			);
		powman_vreg_update_in_progress_wait();
	}
#endif
	set_sys_clock_khz(khz, true);
	sleep_ms(50);
}

/* eof */
