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

void oled_display_status(const struct fanpico_state *state,
	const struct fanpico_config *conf)
{
	char buf[255], l[32], r[32];
	int i, idx;
	double rpm, pwm, temp;
	static uint32_t counter = 0;

	if (!oled_found || !state)
		return;


	for (i = 0; i < 8; i++) {
		if (i < FAN_COUNT) {
			rpm = state->fan_freq[i] * 60 / conf->fans[i].rpm_factor;
			pwm = state->fan_duty[i];
			snprintf(l,sizeof(l),"%d:%4.0lf %3.0lf%% ", i + 1, rpm, pwm);
		} else {
			snprintf(l,sizeof(l),"          ");
		}

		if (i == 0) {
			snprintf(r, sizeof(r), "Inputs   ");
		} else if (i > 0 && i < 4) {
			idx = i - 1;
			pwm = state->mbfan_duty[idx];
			snprintf(r, sizeof(r), " %d:%3.0lf%%  ", idx + 1, pwm);
		} else if (i == 4) {
			snprintf(r, sizeof(r), "Temps    ");
		} else if (i > 4 && i < 8) {
			idx = i - 5;
			temp = state->temp[idx];
			snprintf(r, sizeof(r), " %d:%4.1lfC ", idx + 1, temp);
		} else {
			snprintf(r,sizeof(r),"        ");
		}

		memcpy(&buf[0], l, 12);
		memcpy(&buf[12], r, 10);
		buf[22] = 0;

		if (i == 0) {
			buf[20] = (counter % 2 == 0 ? '*' : ' ');
		}

		oledWriteString(&oled, 0, 0, i, buf, FONT_6x8, 0, 1);
	}

	counter++;
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

void display_status(const struct fanpico_state *state,
	const struct fanpico_config *config)
{
#ifdef OLED_DISPLAY
	oled_display_status(state, config);
#endif
}



