/* display_oled.c
   Copyright (C) 2022-2023 Timo Kokkonen <tjko@iki.fi>

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
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/rtc.h"

#include "ss_oled.h"
#include "fanpico.h"

#ifdef OLED_DISPLAY

static SSOLED oled;
static uint8_t ucBuffer[(128*128)/8];
static uint8_t oled_width = 128;
static uint8_t oled_height = 64;
static uint8_t oled_found = 0;


void oled_display_init()
{
	int res;
	int retries = 0;
	int dtype = OLED_128x64;
	int invert = 0;
	int flip = 0;
	uint8_t brightness = 50; /* display brightness: 0..100 */
	uint8_t disp_brightness = 0;
	int val;
	char *tok, *args, *saveptr;

	/* Check if display settings are configured using SYS:DISP command... */
	if (cfg) {
		args = strdup(cfg->display_type);
		if (args) {
			tok = strtok_r(args, ",", &saveptr);
			while (tok) {
				if (!strncmp(tok, "132x64", 6))
					dtype = OLED_132x64;
				if (!strncmp(tok, "128x128", 7)) {
					dtype = OLED_128x128;
					oled_height = 128;
				}
				else if (!strncmp(tok, "invert", 6))
					invert = 1;
				else if (!strncmp(tok, "flip", 4))
					flip =1;
				else if (!strncmp(tok, "brightness=", 11)) {
					if (str_to_int(tok + 11, &val, 10)) {
						if (val >=0 && val <= 100)
							brightness = val;
					}
				}

				tok = strtok_r(NULL, ",", &saveptr);
			}
			free(args);
		}
	}

	disp_brightness = (brightness / 100.0) * 255;
	log_msg(LOG_DEBUG, "Set display brightness: %u%% (0x%x)\n",
		brightness, disp_brightness);

	log_msg(LOG_NOTICE, "Initializing OLED Display...");
	do {
		sleep_ms(50);
		res = oledInit(&oled, dtype, -1, flip, invert, I2C_HW,
			SDA_PIN, SCL_PIN, -1, 1000000L);
	} while (res == OLED_NOT_FOUND && retries++ < 10);

	if (res == OLED_NOT_FOUND) {
		log_msg(LOG_ERR, "No OLED Display Connected!");
		return;
	}

	switch (res) {
	case OLED_SSD1306_3C:
		log_msg(LOG_NOTICE, "OLED Display: SSD1306 (at 0x3c)");
		break;
	case OLED_SSD1306_3D:
		log_msg(LOG_NOTICE, "OLED Display: SSD1306 (at 0x3d)");
		break;
	case OLED_SH1106_3C:
		log_msg(LOG_NOTICE, "OLED Display: SH1106 (at 0x3c)");
		break;
	case OLED_SH1106_3D:
		log_msg(LOG_NOTICE, "OLED Display: SH1106 (at 0x3d)");
		break;
	case OLED_SH1107_3C:
		log_msg(LOG_NOTICE, "OLED Display: SH1107 (at 0x3c)");
		break;
	case OLED_SH1107_3D:
		log_msg(LOG_NOTICE, "OLED Display: SH1107 (at 0x3d)");
		break;
	default:
		log_msg(LOG_ERR, "Unknown OLED Display.");
	}

	/* Initialize screen. */
	oledSetBackBuffer(&oled, ucBuffer);
	oledFill(&oled, 0, 1);
	oledSetContrast(&oled, disp_brightness);

	/* Display model and firmware version. */
	int y_o = (oled_height > 64 ? 4 : 0);
	oledWriteString(&oled, 0, 15, y_o + 1, "FanPico-" FANPICO_MODEL, FONT_8x8, 0, 1);
	oledWriteString(&oled, 0, 40, y_o + 3, "v" FANPICO_VERSION, FONT_8x8, 0, 1);
	oledWriteString(&oled, 0, 20, y_o + 6, "Initializing...", FONT_6x8, 0, 1);

	oled_found = 1;
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
	char buf[64];
	int i;
	double rpm, pwm, temp;
	datetime_t t;
	static uint32_t counter = 0;
	static int bg_drawn = 0;


	if (!oled_found || !state)
		return;

	if (!bg_drawn) {
		oled_clear_display();
		if (oled_height > 64) {
			oledWriteString(&oled, 0,  0, 0, "Fans", FONT_6x8, 0, 1);
			oledWriteString(&oled, 0, 74, 0, "MB Inputs", FONT_6x8, 0, 1);
			oledWriteString(&oled, 0, 74, 6, "Sensors", FONT_6x8, 0, 1);
			oledDrawLine(&oled, 72, 44, oled_width - 1, 44, 1);
			oledDrawLine(&oled, 72,  0, 72, 79, 1);
		} else {
			oledDrawLine(&oled, 70, 35, oled_width - 1, 35, 1);
			oledDrawLine(&oled, 70,  0, 70, 63, 1);
		}
		bg_drawn = 1;
	}

	if (oled_height <= 64) {
		for (i = 0; i < FAN_COUNT; i++) {
			rpm = state->fan_freq[i] * 60 / conf->fans[i].rpm_factor;
			pwm = state->fan_duty[i];
			snprintf(buf, sizeof(buf), "%d:%4.0lf %3.0lf%%", i + 1, rpm, pwm);
			oledWriteString(&oled, 0 , 0, i, buf, FONT_6x8, 0, 1);
		}
		for (i = 0; i < MBFAN_COUNT; i++) {
			pwm = state->mbfan_duty[i];
			snprintf(buf, sizeof(buf), "%d: %4.0lf%%  ", i + 1, pwm);
			oledWriteString(&oled, 0 , 74, i + 0, buf, FONT_6x8, 0, 1);
		}
		for (i = 0; i < SENSOR_COUNT; i++) {
			temp = state->temp[i];
			snprintf(buf, sizeof(buf), "%d:%5.1lfC ", i + 1, temp);
			if (i == 2) {
				buf[8] = (counter++ % 2 == 0 ? '*' : ' ');
			}
			oledWriteString(&oled, 0 , 74, i + 5, buf, FONT_6x8, 0, 1);
		}
	}
	else {
		for (i = 0; i < FAN_COUNT; i++) {
			rpm = state->fan_freq[i] * 60 / conf->fans[i].rpm_factor;
			pwm = state->fan_duty[i];
			snprintf(buf, sizeof(buf), "%d:%4.0lf %3.0lf%% ", i + 1, rpm, pwm);
			oledWriteString(&oled, 0 , 0, i + 1, buf, FONT_6x8, 0, 1);
		}
		for (i = 0; i < MBFAN_COUNT; i++) {
			pwm = state->mbfan_duty[i];
			snprintf(buf, sizeof(buf), "%d: %4.0lf%%  ", i + 1, pwm);
			oledWriteString(&oled, 0 , 78, i + 1, buf, FONT_6x8, 0, 1);
		}
		for (i = 0; i < SENSOR_COUNT; i++) {
			temp = state->temp[i];
			snprintf(buf, sizeof(buf), "%d:%5.1lfC ", i + 1, temp);
			oledWriteString(&oled, 0 , 78, i + 7, buf, FONT_6x8, 0, 1);
		}

		/* IP */
		const char *ip = network_ip();
		if (ip) {
			/* Center align the IP string */
			int delta = (15 + 1) - strlen(ip);
			if (delta < 0)
				delta = 0;
			int offset = delta / 2;
			memset(buf, ' ', 8);
			snprintf(buf + offset, sizeof(buf), " %s", ip);
			oledWriteString(&oled, 0, 10 + (delta % 2 ? 3 : 0), 15, buf, FONT_6x8, 0, 1);
		}

		/* Uptime & NTP time */
		uint32_t secs = to_us_since_boot(get_absolute_time()) / 1000000;
		uint32_t mins =  secs / 60;
		uint32_t hours = mins / 60;
		uint32_t days = hours / 24;
		snprintf(buf, sizeof(buf), "%03lu+%02lu:%02lu:%02lu",
			days,
			hours % 24,
			mins % 60,
			secs % 60);
		if (rtc_get_datetime(&t)) {
			oledWriteString(&oled, 0, 28, 14, buf, FONT_6x8, 0, 1);
			snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.hour, t.min, t.sec);
			oledWriteString(&oled, 0, 16, 11, buf, FONT_12x16, 0, 1);
		} else {
			oledWriteString(&oled, 0, 28, 12, buf, FONT_6x8, 0, 1);
		}
	}
}

void oled_display_message(int rows, const char **text_lines)
{
	int screen_rows = oled_height / 8;
	int start_row = 0;

	if (!oled_found)
		return;

	oled_clear_display();

	if (rows < screen_rows) {
		start_row = (screen_rows - rows) / 2;
	}

	for(int i = 0; i < rows; i++) {
		int row = start_row + i;
		char *text = (char*)text_lines[i];

		if (row >= screen_rows)
			break;
		oledWriteString(&oled, 0, 0, row, (text ? text : ""), FONT_6x8, 0, 1);
	}
}

#endif

/* eof :-) */
