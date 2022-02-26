/* square_wave_gen.h
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

#ifndef SQUARE_WAVE_GEN_H
#define SQUARE_WAVE_GEN_H 1


uint square_wave_gen_load_program(PIO pio);
void square_wave_gen_program_init(PIO pio, uint sm, uint offset, uint pin);
void square_wave_gen_enabled(PIO pio, uint sm, bool enabled);
void square_wave_gen_set_period(PIO pio, uint sm, uint32_t period);
void square_wave_gen_set_freq(PIO pio, uint sm, double freq);

#endif /* SQUARE_WAVE_GEN_H */


