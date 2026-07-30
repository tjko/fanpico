/* util_rp2.h
   Copyright (C) 2026 Timo Kokkonen <tjko@iki.fi>

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

#ifndef UTIL_RP2_H
#define UTIL_RP2_H 1

#include <stdint.h>
#include "config.h"
#ifdef LIB_PICO_CYW43_ARCH
#include "pico/cyw43_arch.h"
#endif


/* util_rp2.c */
uint32_t get_stack_pointer();
uint32_t get_stack_free();
void print_rp2_meminfo();
void print_irqinfo();
const char *rp2_model_str();
const char *pico_serial_str();
int time_passed(absolute_time_t *t, uint32_t ms);
int time_elapsed(absolute_time_t t, uint32_t ms);
int getstring_timeout_ms(char *str, uint32_t maxlen, uint32_t timeout);
float get_rp2_dvdd();
void print_rp2_board_info();
void print_psram_info();
void rp2_memtest();
void rp2_set_sys_clock(uint32_t khz);
int rp2_is_picow();

#ifdef LIB_PICO_CYW43_ARCH
int cyw43_wifi_get_channel(cyw43_t *self, uint32_t *channel);
#endif


#endif /* UTIL_RP2_H */
