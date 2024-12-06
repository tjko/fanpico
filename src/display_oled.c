/* display_oled.c
   Copyright (C) 2022-2024 Timo Kokkonen <tjko@iki.fi>

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

#include "ss_oled.h"
#include "fanpico.h"

#ifdef OLED_DISPLAY

enum layout_item_types {
	BLANK = 0,
	LABEL = 1,
	LINE = 2,
	MBFAN = 3,
	SENSOR = 4,
	VSENSOR = 5,
};

struct layout_item {
	enum layout_item_types type;
	uint8_t idx;
	const char *label;
};

#define R_LAYOUT_MAX 10

static SSOLED oled;
static uint8_t ucBuffer[(128*128)/8];
static uint8_t oled_width = 128;
static uint8_t oled_height = 64;
static uint8_t oled_found = 0;
static uint8_t r_lines = 8;
static struct layout_item r_layout[R_LAYOUT_MAX];


/* Default screen layouts for inputs/sensors */
#ifndef R_LAYOUT_128x64
#define R_LAYOUT_128x64  "M1,M2,M3,M4,-,S1,S2,S3"
#endif
#ifndef R_LAYOUT_128x128
#define R_LAYOUT_128x128 "LMB Inputs,M1,M2,M3,M4,-,LSensors,S1,S2,S3"
#endif


void parse_r_layout(const char *layout)
{
	int i;
	char *tok, *saveptr, *tmp;

	for (i = 0; i < R_LAYOUT_MAX; i++) {
		r_layout[i].type = BLANK;
		r_layout[i].idx = 0;
		r_layout[i].label = NULL;
	}

	if (!layout)
		return;

	tmp = strdup(layout);
	if (!tmp)
		return;

	log_msg(LOG_DEBUG, "parse OLED right layout: '%s", tmp);

	i = 0;
	tok = strtok_r(tmp, ",", &saveptr);
	while (tok && i < r_lines) {
		switch (tok[0]) {
		case '-':
			r_layout[i].type = LINE;
			break;
		case 'L':
			r_layout[i].type = LABEL;
			r_layout[i].idx = strlen(tok) - 1;
			r_layout[i].label = layout + (tok - tmp) + 1;
			break;
		case 'M':
			r_layout[i].type = MBFAN;
			r_layout[i].idx = clamp_int(atoi(tok + 1), 1, MBFAN_COUNT) - 1;
			break;
		case 'S':
			r_layout[i].type = SENSOR;
			r_layout[i].idx = clamp_int(atoi(tok + 1), 1, SENSOR_COUNT) - 1;
			break;
		case 'V':
			r_layout[i].type = VSENSOR;
			r_layout[i].idx = clamp_int(atoi(tok + 1), 1, VSENSOR_COUNT) - 1;
			break;

		};

		tok = strtok_r(NULL, ",", &saveptr);
		i++;
	}

	free(tmp);
}


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
				if (!strncmp(tok, "none", 4))
					dtype = -1;
				else if (!strncmp(tok, "132x64", 6))
					dtype = OLED_132x64;
				else if (!strncmp(tok, "128x128", 7)) {
					dtype = OLED_128x128;
					oled_height = 128;
					r_lines = 10;
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

	if (dtype < 0) {
		log_msg(LOG_NOTICE, "OLED display support disabled.");
		return;
	}

	if (strlen(cfg->display_layout_r) > 1) {
		parse_r_layout(cfg->display_layout_r);
	} else {
		parse_r_layout(r_lines == 8 ? R_LAYOUT_128x64 : R_LAYOUT_128x128);
	}

	disp_brightness = (brightness / 100.0) * 255;
	log_msg(LOG_DEBUG, "Set display brightness: %u%% (0x%x)\n",
		brightness, disp_brightness);

	log_msg(LOG_NOTICE, "Initializing OLED Display...");
	do {
		sleep_ms(50);
		res = oledInit(&oled, dtype, -1, flip, invert, I2C_HW,
			SDA_PIN, SCL_PIN, LCD_RESET_PIN, cfg->i2c_speed, true);
	} while (res == OLED_NOT_FOUND && retries++ < 10);

	if (res == OLED_NOT_FOUND) {
		log_msg(LOG_ERR, "No OLED Display Connected!");
		return;
	}

	char *disp_model = "";
	uint8_t disp_addr = 0;

	switch (res) {
	case OLED_SSD1306_3C:
		disp_model = "SSD1306";
		disp_addr = 0x3c;
		break;
	case OLED_SSD1306_3D:
		disp_model = "SSD1306";
		disp_addr = 0x3d;
		break;
	case OLED_SH1106_3C:
		disp_model = "SH1106";
		disp_addr = 0x3c;
		break;
	case OLED_SH1106_3D:
		disp_model = "SH1106";
		disp_addr = 0x3d;
		break;
	case OLED_SH1107_3C:
		disp_model = "SH1107";
		disp_addr = 0x3c;
		break;
	case OLED_SH1107_3D:
		disp_model = "SH1107";
		disp_addr = 0x3d;
		break;
	default:
		disp_model = "Unknown";
	}
	log_msg(LOG_NOTICE, "I2C OLED Display: %s (at %02x)", disp_model, disp_addr);

	/* Initialize screen. */
	oledSetBackBuffer(&oled, ucBuffer);
	oledFill(&oled, 0, 1);
	oledSetContrast(&oled, disp_brightness);

	/* Display model and firmware version. */
	int y_o = (oled_height > 64 ? 4 : 0);
	oledWriteString(&oled, 0, 15, y_o + 1, "FanPico-" FANPICO_MODEL, FONT_8x8, 0, 1);
	oledWriteString(&oled, 0, 40, y_o + 3, "v" FANPICO_VERSION, FONT_8x8, 0, 1);
	oledWriteString(&oled, 0, 40, y_o + 4, " " FANPICO_BUILD_TAG, FONT_8x8, 0, 1);
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
	struct tm t;
	static uint32_t counter = 0;
	static int bg_drawn = 0;


	if (!oled_found || !state)
		return;

	int h_pos = 70;
	int fan_row_offset = (oled_height > 64 ? 1 : 0);

	if (!bg_drawn) {
		/* Draw "background" only once... */
		oled_clear_display();

		if (oled_height > 64) {
			oledWriteString(&oled, 0,  0, 0, "Fans", FONT_6x8, 0, 1);
		}

		for (i = 0; i < r_lines; i++) {
			struct layout_item *l = &r_layout[i];
			char label[16];
			int y = i * 8;

			if (l->type == LINE) {
				oledDrawLine(&oled, h_pos, y + 4, oled_width - 1, y + 4, 1);
			}
			else if (l->type == LABEL) {
				int len = l->idx;
				if (len >= sizeof(label))
					len = sizeof(label) - 1;
				memcpy(label, l->label, len);
				label[len] = 0;
				oledWriteString(&oled, 0, h_pos + 2, i, label, FONT_6x8, 0, 1);
			}
		}
		oledDrawLine(&oled, h_pos, 0, h_pos, r_lines * 8 - 1, 1);
		bg_drawn = 1;
	}


	for (i = 0; i < FAN_COUNT; i++) {
		rpm = state->fan_freq[i] * 60 / conf->fans[i].rpm_factor;
		pwm = state->fan_duty[i];
		snprintf(buf, sizeof(buf), "%d:%4.0lf %3.0lf%%", i + 1, rpm, pwm);
		oledWriteString(&oled, 0 , 0, i + fan_row_offset, buf, FONT_6x8, 0, 1);
	}
	for (i = 0; i < r_lines; i++) {
		struct layout_item *l = &r_layout[i];
		int write_buf = 0;

		if (l->type == MBFAN) {
			pwm = state->mbfan_duty[l->idx];
			snprintf(buf, sizeof(buf), "%d: %4.0lf%%  ", l->idx + 1, pwm);
			write_buf = 1;
		}
		else if (l->type == SENSOR) {
			temp = state->temp[l->idx];
			snprintf(buf, sizeof(buf), "s%d:%5.1lfC", l->idx + 1, temp);
			write_buf = 1;
		}
		else if (l->type ==  VSENSOR) {
			temp = state->vtemp[l->idx];
			snprintf(buf, sizeof(buf), "v%d:%5.1lfC", l->idx + 1, temp);
			write_buf = 1;
		}
		if (write_buf) {
			if (oled_height <= 64 && i == 0) {
				buf[8] = (counter++ % 2 == 0 ? '*' : ' ');
			}
			oledWriteString(&oled, 0 , h_pos + 2, i, buf, FONT_6x8, 0, 1);
		}
		else if (FAN_COUNT > 4 && i == 0 && oled_height <= 64) {
			/* Handle case where first row has static content... */
			buf[0] = (counter++ % 2 == 0 ? '*' : ' ');
			buf[1] = 0;
			oledWriteString(&oled, 0, h_pos + 2 + (8*6), i, buf, FONT_6x8, 0, 1);
		}
	}


	/* Uptime & NTP time */
	uint32_t secs = to_us_since_boot(get_absolute_time()) / 1000000;
	uint32_t mins =  secs / 60;
	uint32_t hours = mins / 60;
	uint32_t days = hours / 24;

	if (oled_height > 64) {
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
		/* Uptime / Clock */
		snprintf(buf, sizeof(buf), "%03lu+%02lu:%02lu:%02lu",
			days % 1000,
			hours % 24,
			mins % 60,
			secs % 60);
		if (rtc_get_tm(&t)) {
			oledWriteString(&oled, 0, 28, 14, buf, FONT_6x8, 0, 1);
			snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
			oledWriteString(&oled, 0, 16, 11, buf, FONT_12x16, 0, 1);
		} else {
			oledWriteString(&oled, 0, 28, 12, buf, FONT_6x8, 0, 1);
		}
	}
	else if (FAN_COUNT <= 4) {
		/* Uptime / Clock */
		snprintf(buf, sizeof(buf), "%03lu+%02lu:%02lu",
			days % 1000,
			hours % 24,
			mins % 60);
		oledWriteString(&oled, 0, 6, 7, buf, FONT_6x8, 0, 1);
		if (rtc_get_tm(&t)) {
			snprintf(buf, sizeof(buf), "%02d%c%02d",
				t.tm_hour, (t.tm_sec % 2 ? ':' : ' '), t.tm_min);
			oledWriteString(&oled, 0, 3, 5, buf, FONT_12x16, 0, 1);
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
