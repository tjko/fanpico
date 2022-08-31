/* display.c
   Copyright (C) 2022 Timo Kokkonen <tjko@iki.fi>

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
#include <math.h>
#include <assert.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"


#include "fanpico.h"

#ifdef OLED_DISPLAY
#include "ss_oled.h"

SSOLED oled;
uint8_t oled_found = 0;

void oled_display_init()
{
	int res;

	res = oledInit(&oled, OLED_128x64, -1, 0, 0, 1, SDA_PIN, SCL_PIN, -1, 1000000L);

	if (res == OLED_NOT_FOUND) {
		printf("Display not found\n");
		return;
	}
	oled_found = 1;

	oledFill(&oled, 0, 1);
	oledSetContrast(&oled, 127);
	oledWriteString(&oled, 0, 15, 1, "FanPico-" FANPICO_MODEL, FONT_8x8, 0, 1);
	oledWriteString(&oled, 0, 40, 3, "v" FANPICO_VERSION, FONT_8x8, 0, 1);

	oledWriteString(&oled, 0, 20, 6, "Initializing...", FONT_6x8, 0, 1);
}


void oled_clear_display()
{
	if (!oled_found)
		return;

	oledFill(&oled, 0, 1);
}

void oled_display_status(const struct fanpico_state *state)
{
	absolute_time_t t_now;
	char buf[255];

	if (!oled_found || !state)
		return;

	t_now = get_absolute_time();
	snprintf(buf, sizeof(buf), "Ticks: %llu", t_now);

	oledWriteString(&oled, 0, 0, 0, "abcdefghijlkmnopqrstuvwxyz", FONT_6x8, 0, 1);
	oledWriteString(&oled, 0, 0, 1, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", FONT_6x8, 0, 1);
	oledWriteString(&oled, 0, 0, 2, "123456789012345678901234567890", FONT_6x8, 0, 1);
	oledWriteString(&oled, 0, 0, 3, "---------|---------|---------|", FONT_6x8, 0, 1);
	oledWriteString(&oled, 0, 0, 4, "---------|---------|---------|", FONT_6x8, 0, 1);
	oledWriteString(&oled, 0, 0, 5, "---------|---------|---------|", FONT_6x8, 0, 1);
	oledWriteString(&oled, 0, 0, 6, "---------|---------|---------|", FONT_6x8, 0, 1);
	oledWriteString(&oled, 0, 0, 7, buf , FONT_6x8, 0, 1);
}

#endif /* OLED_DISPLAY */


void display_init()
{
#ifdef OLED_DISPLAY
	oled_display_init();
#endif
}

void clear_display()
{
#ifdef OLED_DISPLAY
	oled_clear_display();
#endif
}

void display_status(const struct fanpico_state *state)
{
#ifdef OLED_DISPLAY
	oled_display_status(state);
#endif
}



