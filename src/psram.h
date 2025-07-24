/* psram.h
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

#ifndef FANPICO_PSRAM_H
#define FANPICO_PSRAM_H 1

#include "hardware/platform_defs.h"
#include "hardware/regs/addressmap.h"

#define PSRAM_BASE         _u(0x11000000)
#define PSRAM_NOCACHE_BASE _u(0x15000000)



#ifndef FANPICO_PSRAM_PIN
 #ifdef PIMORONI_PGA2350_PSRAM_CS_PIN
 #define FANPICO_PSRAM_PIN PIMORONI_PGA2350_PSRAM_CS_PIN
 #elifdef PIMORONI_PICO_PLUS2_PSRAM_CS_PIN
 #define FANPICO_PSRAM_PIN PIMORONI_PICO_PLUS2_PSRAM_CS_PIN
 #elifdef PIMORONI_PICO_PLUS2_W_PSRAM_CS_PIN
 #define FANPICO_PSRAM_PIN PIMORONI_PICO_PLUS2_W_PSRAM_CS_PIN
 #elifdef WEACT_STUDIO_RP2350B_PSRAM_CS_PIN
 #define FANPICO_PSRAM_PIN WEACT_STUDIO_RP2350B_PSRAM_CS_PIN
 #endif
#endif

void setup_psram();
size_t psram_size();
const char* psram_manufacturer();
const char* psram_id_str();


#endif /* FANPICO_PSRAM_H */


