/* pulse_len.h
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

#ifndef PULSE_LEN_H
#define PULSE_LEN_H 1

uint64_t pulse_measure(int pin, bool type, bool mode, uint32_t timeout);
uint64_t pulseIn(int gpio, int value, uint32_t timeout_ms);

void pulse_setup_interrupt(uint gpio, uint32_t events);
void pulse_disable_interrupt();
uint64_t pulse_interval();




#endif /* PULSE_LEN_H */


