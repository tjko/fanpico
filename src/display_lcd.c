/* display_lcd.c
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

#include "bb_spi_lcd.h"
#include "fanpico.h"


static SPILCD lcd;
static uint8_t lcd_found = 0;

#define LCD_LOGO_WIDTH 160
#define LCD_LOGO_HEIGHT 140
extern uint8_t fanpico_lcd_logo_bmp[]; /* ptr to embedded lcd-logo.bmp */
extern uint8_t fanpico_lcd_bg_1_bmp[]; /* ptr to embedded lcd-background-1.bmp */

/* Macros for converting RGB888 colorspace to RGB565 */

/* Convert 24bit color space to 16bit color space.  */
#define RGB565(rgb) (uint16_t)( ((rgb & 0xf80000) >> 8) | ((rgb & 0xfc00) >> 5) | ((rgb & 0xf8) >> 3) )
/* Generate 16bit color from 8bit RGB components. */
#define RGB565C(r, g, b) (uint16_t)( ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | ((b & 0xf8) >> 3) )

static uint16_t bg_color = RGB565(0x006163);  // 0x006567
static uint16_t box_color = RGB565(0x008e4a);
static uint16_t box2_color = RGB565(0x049467);
static uint16_t text_color = RGB565(0x07a3aa);


struct lcd_type {
	const char* name;
	int dtype;
	int flags;
	int orientation;
	int32_t spi_freq;
	const uint8_t *custom_init;
	uint32_t custom_init_len;
	int invert_offset;
	int rgb_offset;
};

enum display_field_types {
	INVALID_FIELDTYPE = 0,
	FAN,
	MBFAN,
	SENSOR,
	UPTIME,
	TIME,
	DATE,
	IP,
	DISPLAY_FIELD_TYPE_COUNT
};

enum display_data_types {
	INVALID_DATATYPE = 0,
	LABEL,
	RPM,
	PWM,
	TEMP,
	OTHER,
	DISPLAY_DATA_TYPE_COUNT
};

typedef struct display_field {
	enum display_field_types type;
	enum display_data_types data;
	int id;
	int x;
	int y;
	uint16_t fg;
	uint16_t bg;
	int font;
} display_field_t;

struct display_fields {
	display_field_t *bg; /* background items, draw only once */
	display_field_t *fg; /* foreground items */
};


display_field_t theme_1_bg[] = {
	{ FAN, LABEL, 0, 3,  34, RGB565(0x000000), RGB565(0xffcc66), FONT_8x8 },
	{ FAN, LABEL, 1, 3,  54, RGB565(0x000000), RGB565(0xffff99), FONT_8x8 },
	{ FAN, LABEL, 2, 3,  74, RGB565(0x000000), RGB565(0xcc99cc), FONT_8x8 },
	{ FAN, LABEL, 3, 3,  94, RGB565(0x000000), RGB565(0xcc6699), FONT_8x8 },
	{ FAN, LABEL, 4, 3, 114, RGB565(0x000000), RGB565(0x99ccff), FONT_8x8 },
	{ FAN, LABEL, 5, 3, 134, RGB565(0x000000), RGB565(0xff9966), FONT_8x8 },
	{ FAN, LABEL, 6, 3, 154, RGB565(0x000000), RGB565(0xffcc66), FONT_8x8 },
	{ FAN, LABEL, 7, 3, 174, RGB565(0x000000), RGB565(0xffff99), FONT_8x8 },

	{ MBFAN, LABEL, 0, 3, 212, RGB565(0x000000), RGB565(0xcc6699), FONT_8x8 },
	{ MBFAN, LABEL, 1, 3, 232, RGB565(0x000000), RGB565(0xffcc66), FONT_8x8 },
	{ MBFAN, LABEL, 2, 3, 252, RGB565(0x000000), RGB565(0xcc99cc), FONT_8x8 },
	{ MBFAN, LABEL, 3, 3, 272, RGB565(0x000000), RGB565(0xcc6666), FONT_8x8 },

	{ SENSOR, LABEL, 0, 276, 212, RGB565(0x000000), RGB565(0x6688cc), FONT_8x8 },
	{ SENSOR, LABEL, 1, 276, 232, RGB565(0x000000), RGB565(0x9977aa), FONT_8x8 },
	{ SENSOR, LABEL, 2, 276, 252, RGB565(0x000000), RGB565(0x774466), FONT_8x8 },

	{ 0, 0, -1, -1, -1, 0, 0, -1 }
};

display_field_t theme_1_fg[] = {
	{ FAN, RPM, 0, 128,  32, RGB565(0xffcc66), RGB565(0x000000), FONT_12x16 },
	{ FAN, RPM, 1, 128,  52, RGB565(0xffff99), RGB565(0x000000), FONT_12x16 },
	{ FAN, RPM, 2, 128,  72, RGB565(0xcc99cc), RGB565(0x000000), FONT_12x16 },
	{ FAN, RPM, 3, 128,  92, RGB565(0xcc6699), RGB565(0x000000), FONT_12x16 },
	{ FAN, RPM, 4, 128, 112, RGB565(0x99ccff), RGB565(0x000000), FONT_12x16 },
	{ FAN, RPM, 5, 128, 132, RGB565(0xff9966), RGB565(0x000000), FONT_12x16 },
	{ FAN, RPM, 6, 128, 152, RGB565(0xffcc66), RGB565(0x000000), FONT_12x16 },
	{ FAN, RPM, 7, 128, 172, RGB565(0xffff99), RGB565(0x000000), FONT_12x16 },
	{ FAN, PWM, 0, 200,  32, RGB565(0xffcc66), RGB565(0x000000), FONT_12x16 },
	{ FAN, PWM, 1, 200,  52, RGB565(0xffff99), RGB565(0x000000), FONT_12x16 },
	{ FAN, PWM, 2, 200,  72, RGB565(0xcc99cc), RGB565(0x000000), FONT_12x16 },
	{ FAN, PWM, 3, 200,  92, RGB565(0xcc6699), RGB565(0x000000), FONT_12x16 },
	{ FAN, PWM, 4, 200, 112, RGB565(0x99ccff), RGB565(0x000000), FONT_12x16 },
	{ FAN, PWM, 5, 200, 132, RGB565(0xff9966), RGB565(0x000000), FONT_12x16 },
	{ FAN, PWM, 6, 200, 152, RGB565(0xffcc66), RGB565(0x000000), FONT_12x16 },
	{ FAN, PWM, 7, 200, 172, RGB565(0xffff99), RGB565(0x000000), FONT_12x16 },

	{ MBFAN, RPM, 0, 128, 210, RGB565(0xcc6699), RGB565(0x000000), FONT_12x16 },
	{ MBFAN, RPM, 1, 128, 230, RGB565(0xffcc66), RGB565(0x000000), FONT_12x16 },
	{ MBFAN, RPM, 2, 128, 250, RGB565(0xcc99cc), RGB565(0x000000), FONT_12x16 },
	{ MBFAN, RPM, 3, 128, 270, RGB565(0xcc6666), RGB565(0x000000), FONT_12x16 },
	{ MBFAN, PWM, 0, 200, 210, RGB565(0xcc6699), RGB565(0x000000), FONT_12x16 },
	{ MBFAN, PWM, 1, 200, 230, RGB565(0xffcc66), RGB565(0x000000), FONT_12x16 },
	{ MBFAN, PWM, 2, 200, 250, RGB565(0xcc99cc), RGB565(0x000000), FONT_12x16 },
	{ MBFAN, PWM, 3, 200, 270, RGB565(0xcc6666), RGB565(0x000000), FONT_12x16 },

	{ SENSOR, TEMP, 0, 401, 210, RGB565(0x6688cc), RGB565(0x000000), FONT_12x16 },
	{ SENSOR, TEMP, 1, 401, 230, RGB565(0x9977aa), RGB565(0x000000), FONT_12x16 },
	{ SENSOR, TEMP, 2, 401, 250, RGB565(0x774466), RGB565(0x000000), FONT_12x16 },

	{ IP, OTHER, 0, 344, 308, RGB565(0x000000), RGB565(0xff9900), FONT_8x8 },
	{ UPTIME, OTHER, 0, 292, 48, RGB565(0x000000), RGB565(0xcc99cc), FONT_12x16 },
	{ DATE, OTHER, 0, 300, 90, RGB565(0xff9900), RGB565(0x000000), FONT_12x16 },
	{ TIME, OTHER, 0, 300, 118, RGB565(0xffcc66), RGB565(0x000000), FONT_16x32 },

	{ 0, 0, -1, -1, -1, 0, 0, -1 }
};

struct display_fields theme_1 = {theme_1_bg, theme_1_fg};

const uint8_t waveshare35a[] = {
	1, 0x01,
	LCD_DELAY, 50,
	1, 0x28,
	2, 0xb0, 0x00,
	1, 0x11,
	1, 0x20,   // no-invert [12]
	2, 0x3a, 0x55,
	2, 0x36, 0x28, // RGB mode [18]

	2, 0xc2, 0x44,
	5, 0xc5, 0x00, 0x00, 0x00, 0x00,
	16, 0xe0, 0x0f, 0x1f, 0x1c, 0x0c, 0x0f, 0x08, 0x48, 0x98, 0x37, 0x0a, 0x13, 0x04, 0x11, 0x0d, 0x00,
	16, 0xe1, 0x0f, 0x32, 0x2e, 0x0b, 0x0d, 0x05, 0x47, 0x75, 0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00,
	16, 0xe2, 0x0f, 0x32, 0x2e, 0x0b, 0x0d, 0x05, 0x47, 0x75, 0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00,

	1, 0x11,
	LCD_DELAY, 150,
	1, 0x29,
	LCD_DELAY, 25,
	0
};

const uint8_t waveshare35b[] = {
	1, 0x01,
	LCD_DELAY, 50,
	1, 0x28,
	2, 0xb0, 0x00,
	1, 0x11,
	1, 0x21,   // invert [12]
	2, 0x3a, 0x55,
	2, 0x36, 0x28, // RGB mode [18]

	3, 0xc0, 0x09, 0x09,
	3, 0xc1, 0x41, 0x00,
	3, 0xc5, 0x00, 0x36,
	16, 0xe0, 0x00, 0x2c, 0x2c, 0x0b, 0x0c, 0x04, 0x4c, 0x64, 0x36, 0x03, 0x0e, 0x01, 0x10, 0x01, 0x00,
	16, 0xe1, 0x0f, 0x37, 0x37, 0x0c, 0x0f, 0x05, 0x50, 0x32, 0x36, 0x04, 0x0b, 0x00, 0x19, 0x14, 0x0f,

	1, 0x11,
	LCD_DELAY, 150,
	1, 0x29,
	LCD_DELAY, 25,
	0
};

const uint8_t waveshare35bv2[] = {
	1, 0x01,
	LCD_DELAY, 50,
	1, 0x28,
	2, 0xb0, 0x00,
	1, 0x11,
	1, 0x21,   // invert [12]
	2, 0x3a, 0x55,
	2, 0x36, 0x28, // RGB mode [18]

	2, 0xc2, 0x33,
	4, 0xc5, 0x00, 0x1e, 0x80,
	2, 0xb1, 0xb0,
	16, 0xe0, 0x00, 0x13, 0x18, 0x04, 0x0f, 0x06, 0x3a, 0x56, 0x4d, 0x03, 0x0a, 0x06, 0x30, 0x3e, 0x0f,
	16, 0xe1, 0x00, 0x13, 0x18, 0x01, 0x11, 0x06, 0x38, 0x34, 0x4d, 0x06, 0x0d, 0x0b, 0x31, 0x37, 0x0f,

	1, 0x11,
	LCD_DELAY, 150,
	1, 0x29,
	LCD_DELAY, 25,
	0
};

const struct lcd_type lcd_types[] = {
	/* Generic controllers/panels */
	{"ILI9341", LCD_ILI9341, FLAGS_NONE, LCD_ORIENTATION_90, -1, NULL, 0, -1, -1},
//	{"ILI9342", LCD_ILI9342, FLAGS_NONE, LCD_ORIENTATION_90, -1, NULL, 0, -1, -1},
	{"ST7789", LCD_ST7789, FLAGS_NONE, LCD_ORIENTATION_90, -1, NULL, 0, -1, -1},
	{"ILI9486", LCD_ILI9486, FLAGS_NONE, LCD_ORIENTATION_90, -1, NULL, 0, -1, -1},
	{"HX8357", LCD_HX8357, FLAGS_NONE, LCD_ORIENTATION_90, -1, NULL, 0, -1, -1},

	/* Panel/module specific support */
	{"waveshare35a", LCD_ILI9486, FLAGS_16BIT|FLAGS_SWAP_RB|FLAGS_SWAP_X, LCD_ORIENTATION_90, -1,
	 waveshare35a, sizeof(waveshare35a), 12, 18},
	{"waveshare35b", LCD_ILI9486, FLAGS_16BIT|FLAGS_SWAP_RB|FLAGS_SWAP_X, LCD_ORIENTATION_90, -1,
	 waveshare35b, sizeof(waveshare35b), 12, 18},
	{"waveshare35bv2", LCD_ILI9486, FLAGS_16BIT|FLAGS_SWAP_RB|FLAGS_SWAP_X, LCD_ORIENTATION_90, -1,
	 waveshare35bv2, sizeof(waveshare35bv2), 12, 18},

	{NULL, -1, -1, -1, -1, NULL, 0, -1, -1}
};

void draw_rounded_box(SPILCD *lcd, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t r, uint16_t color)
{
	spilcdRectangle(lcd, x + r, y, width - (2 * r), r, color, color, 1, DRAW_TO_LCD);
	spilcdRectangle(lcd, x, y + r, width, height - (2 * r), color, color, 1, DRAW_TO_LCD);
	spilcdRectangle(lcd, x + r, y + height - r, width - (2 * r), r, color, color, 1, DRAW_TO_LCD);

	spilcdEllipse(lcd, x + r, y + r, r, r, color, 1, DRAW_TO_LCD);
	spilcdEllipse(lcd, x + width - r - 1, y + r, r, r, color, 1, DRAW_TO_LCD);
	spilcdEllipse(lcd, x + r, y + height - r, r, r, color, 1, DRAW_TO_LCD);
	spilcdEllipse(lcd, x + width - r - 1, y + height - r, r, r, color, 1, DRAW_TO_LCD);
}

void lcd_display_init()
{
	int dtype = LCD_INVALID;
	int flags = FLAGS_NONE;
	int orientation = LCD_ORIENTATION_90;
	int32_t spi_freq = (48 * 1000 * 1000); // Default to 48MHz
	const char *lcd_name;
	int val;
	char *args, *tok, *saveptr;


	if (!cfg)
		return;
	memset(&lcd, 0, sizeof(lcd));

	args = strdup(cfg->display_type);
	if (!args)
		return;
	tok = strtok_r(args, ",", &saveptr);
	while (tok) {
		if (!strncmp(tok,"lcd=", 4)) {
			char *t = tok + 4;
			int i = 0;
			while (lcd_types[i].name) {
				const struct lcd_type *l = &lcd_types[i];
				if (!strcmp(l->name, t)) {
					lcd_name = l->name;
					dtype = l->dtype;
					if (l->flags >= 0)
						flags = l->flags;
					if (l->orientation >= 0)
						orientation = l->orientation;
					if (l->spi_freq >= 0)
						spi_freq = l->spi_freq;
					if (l->custom_init)
						spilcdCustomInit(&lcd, l->custom_init, l->custom_init_len,
								l->invert_offset, l->rgb_offset);
					break;
				}
				i++;
			}
		}
		else if (!strncmp(tok, "rotate=", 7)) {
			if (str_to_int(tok + 7, &val, 10)) {
				switch(val) {
				case 90:
					orientation = LCD_ORIENTATION_90;
					break;
				case 180:
					orientation = LCD_ORIENTATION_180;
					break;
				case 270:
					orientation = LCD_ORIENTATION_270;
					break;
				default:
					orientation = LCD_ORIENTATION_0;
				}
			}
		}
		else if (!strncmp(tok, "spifreq=", 8)) {
			if (str_to_int(tok + 8, &val, 10)) {
				if (val > 0 && val < 1000) {
					spi_freq = val * 1000 * 1000;
				}
				else if (val >= 1000) {
					spi_freq = val;
				}
			}
		}
		else if (!strncmp(tok, "invert", 6))
			flags ^= FLAGS_INVERT;
		else if (!strncmp(tok, "swapcolors", 10))
			flags ^= FLAGS_SWAP_RB;
		else if (!strncmp(tok, "16bit", 5))
			flags |= FLAGS_16BIT;

		tok = strtok_r(NULL, ",", &saveptr);
	}
	free(args);

	if (dtype == LCD_INVALID) {
		log_msg(LOG_NOTICE, "No LCD type configured!");
		return;
	}
	lcd_found = 1;

	log_msg(LOG_NOTICE, "Initializing LCD display...");
	log_msg(LOG_INFO, "SPI Frequency: %ld Hz", spi_freq);

	int res = spilcdInit(&lcd, dtype, flags, spi_freq,
		CS_PIN, DC_PIN, LCD_RESET_PIN, LCD_LIGHT_PIN,
		MISO_PIN, MOSI_PIN, SCK_PIN);
	log_msg(LOG_DEBUG, "spilcdInit(): %d", res);

	spilcdSetOrientation(&lcd, orientation);
	spilcdFill(&lcd, 0, DRAW_TO_LCD);

	log_msg(LOG_NOTICE, "LCD display: %s %dx%d",
		lcd_name,
		lcd.iCurrentWidth, lcd.iCurrentHeight);

	spilcdDrawBMP(&lcd, fanpico_lcd_logo_bmp,
		(lcd.iCurrentWidth - LCD_LOGO_WIDTH) / 2,
		10,
		0, -1, DRAW_TO_LCD);
	int w_c = lcd.iCurrentWidth / 2;
	spilcdWriteString(&lcd, w_c - 100, 165, "FanPico-" FANPICO_MODEL, text_color, 0, FONT_16x16, DRAW_TO_LCD);
	spilcdWriteString(&lcd, w_c - 40, 190, "v" FANPICO_VERSION, text_color, 0, FONT_16x16, DRAW_TO_LCD);
	spilcdWriteString(&lcd, w_c - 50, 220, "Initializing...", text_color, 0, FONT_8x8, DRAW_TO_LCD);
}

void lcd_clear_display()
{
	if (!lcd_found)
		return;

	spilcdFill(&lcd, 0, DRAW_TO_LCD);
}

void draw_fields(const struct fanpico_state *state, const struct fanpico_config *conf, display_field_t *list)
{
	int i = 0;
	char buf[64];
	const char *s;
	double val;
	datetime_t t;


	while (list[i].type > INVALID_FIELDTYPE && list[i].type < DISPLAY_FIELD_TYPE_COUNT) {
		display_field_t *f = &list[i];

		buf[0] = 0;
		switch (f->data) {

		case LABEL:
			switch (f->type) {

			case FAN:
				s = conf->fans[f->id].name;
				break;

			case MBFAN:
				s = conf->mbfans[f->id].name;
				break;

			case SENSOR:
				s = conf->sensors[f->id].name;
				break;

			default:
				s = "";
			}
			snprintf(buf, 15, "%14s", s);
			break;

		case RPM:
			switch (f->type) {

			case FAN:
				val = state->fan_freq[f->id] * 60 / conf->fans[f->id].rpm_factor;
				break;

			case MBFAN:
				val = state->mbfan_freq[f->id] * 60 / conf->mbfans[f->id].rpm_factor;
				break;

			default:
				val = 0.0;
			}
			snprintf(buf, 5, "%4.0lf", val);
			break;

		case PWM:
			switch (f->type) {
			case FAN:
				val = state->fan_duty[f->id];
				break;
			case MBFAN:
				val = state->mbfan_duty[f->id];
				break;
			default:
				val = 0.0;
			}
			snprintf(buf, 4, "%3.0lf", val);
			break;

		case TEMP:
			if (f->type == SENSOR) {
				snprintf(buf,5, "%2.1lf", state->temp[f->id]);
			}
			break;

		case OTHER:
			switch (f->type) {
			case IP:
				snprintf(buf, 16, "%15s", network_ip());
				break;
			case DATE:
				if (rtc_get_datetime(&t)) {
					snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
						t.year, t.month, t.day);
				}
				break;
			case TIME:
				if (rtc_get_datetime(&t)) {
					snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
						t.hour, t.min, t.sec);
				}
				break;
			case UPTIME:
			        {
					uint32_t secs = to_us_since_boot(get_absolute_time()) / 1000000;
					uint32_t mins =  secs / 60;
					uint32_t hours = mins / 60;
					uint32_t days = hours / 24;

					snprintf(buf, 13, "%03lu+%02lu:%02lu:%02lu",
						days,
						hours % 24,
						mins % 60,
						secs % 60);
			        }
				break;

			default:
				break;
			}
			break;

		default:
			buf[0] = 0;
		}

		if (strlen(buf) > 0)
			spilcdWriteString(&lcd, f->x, f->y, buf, f->fg, f->bg, f->font, DRAW_TO_LCD);

		i++;
	}
}

void lcd_display_status(const struct fanpico_state *state,
	const struct fanpico_config *conf)
{
	char buf[96];
	int i;
	double rpm, pwm, temp;
	datetime_t t;
	static uint8_t bg_drawn = 0;
	uint16_t o_x = 0;
	uint16_t o_y = 0;
	uint16_t fans_x = 4 + o_y;
	uint16_t fans_y = 20 + o_y;
	uint16_t mbfans_x = 189 + o_y;
	uint16_t mbfans_y = 20 + o_y;
	uint16_t sensors_x = 189 + o_y;
	uint16_t sensors_y = 130 + o_y;
	uint16_t x, y, b;


	if (!lcd_found || !state)
		return;

	if (!bg_drawn) {
		/* draw background graphics only once... */
		bg_drawn = 1;

		if (lcd.iCurrentWidth >= 480) {
			spilcdDrawBMP(&lcd, fanpico_lcd_bg_1_bmp, 0, 0,	0, -1, DRAW_TO_LCD);
			draw_fields(state, conf, theme_1.bg);
			return;
		} else {
			spilcdFill(&lcd, 0x0000, DRAW_TO_LCD);

			if (bg_color != 0)
				draw_rounded_box(&lcd, o_x + 0, o_y + 0, 320, 240, 10, bg_color);
			//spilcdRectangle(&lcd, 0, 0, lcd.iCurrentWidth -1, lcd.iCurrentHeight - 1, 0xffff, 0xffff, 0, DRAW_TO_LCD);


			/* fans */
			x = fans_x;
			y = fans_y;
			draw_rounded_box(&lcd, x, y, 181, 154, 8, box_color);
			spilcdWriteString(&lcd, 4+x, y - 10, "FANS", 0, bg_color, FONT_8x8, DRAW_TO_LCD);
			spilcdWriteString(&lcd, x+4, y + 4, "#", 0, box_color, FONT_8x8, DRAW_TO_LCD);
			spilcdWriteString(&lcd, x+4+90, y + 4, "RPM", 0, box_color, FONT_8x8, DRAW_TO_LCD);
			spilcdWriteString(&lcd, x+4+140, y + 4, "PWM%", 0, box_color, FONT_8x8, DRAW_TO_LCD);
			for(i = 0; i < FAN_COUNT; i++) {
				b = (i % 2 ? box_color : box2_color);

				spilcdRectangle(&lcd, x+4, y + i*17 + 16, 140, 16, b, b, 1, DRAW_TO_LCD);

				snprintf(buf, sizeof(buf), "<%d>", i + 1);
				spilcdWriteString(&lcd, x+4, y + i*17 + 16, buf, RGB565(0x3a393a), b, FONT_8x8, DRAW_TO_LCD);
				snprintf(buf, sizeof(buf), "%s", conf->fans[i].name);
				buf[14]=0;
				spilcdWriteString(&lcd, x+4, y + i*17 + 16 + 8, buf, 0, b, FONT_6x8, DRAW_TO_LCD);
			}

			/* mbfans */
			x = mbfans_x;
			y = mbfans_y;
			draw_rounded_box(&lcd, x, y, 126, 86, 8, box_color);
			spilcdWriteString(&lcd, x+4, y - 10, "MB FANS", 0, bg_color, FONT_8x8, DRAW_TO_LCD);
			spilcdWriteString(&lcd, x+4, y + 4, "#", 0, box_color, FONT_8x8, DRAW_TO_LCD);
			spilcdWriteString(&lcd, x+4+85, y + 4, "PWM%", 0, box_color, FONT_8x8, DRAW_TO_LCD);
			for(i = 0; i < MBFAN_COUNT; i++) {
				b = (i % 2 ? box_color : box2_color);

				spilcdRectangle(&lcd, x+4, y + i*17 + 16, 120, 16, b, b, 1, DRAW_TO_LCD);

				snprintf(buf, sizeof(buf), "<%d>", i + 1);
				spilcdWriteString(&lcd, x+4, y + i*17 + 16, buf, RGB565(0x3a393a), b, FONT_8x8, DRAW_TO_LCD);
				snprintf(buf, sizeof(buf), "%s", conf->mbfans[i].name);
				buf[13]=0;
				spilcdWriteString(&lcd, x+4, y + i*17 + 16 + 8, buf, 0, b, FONT_6x8, DRAW_TO_LCD);
			}

			/* sensors */
			x = sensors_x;
			y = sensors_y;
			draw_rounded_box(&lcd, x, y, 126, 70, 8, box_color);
			spilcdWriteString(&lcd, x+4, y - 10, "SENSORS", 0, bg_color, FONT_8x8, DRAW_TO_LCD);
			spilcdWriteString(&lcd, x+4, y + 4, "#", 0, box_color, FONT_8x8, DRAW_TO_LCD);
			spilcdWriteString(&lcd, x+4+69, y + 4, "Temp C", 0, box_color, FONT_8x8, DRAW_TO_LCD);
			for(i = 0; i < SENSOR_COUNT; i++) {
				b = (i % 2 ? box_color : box2_color);

				spilcdRectangle(&lcd, x+4, y + i*17 + 16, 120, 16, b, b, 1, DRAW_TO_LCD);

				snprintf(buf, sizeof(buf), "<%d>", i + 1);
				spilcdWriteString(&lcd, x+4, y + i*17 + 16, buf, RGB565(0x3a393a), b, FONT_8x8, DRAW_TO_LCD);
				snprintf(buf, sizeof(buf), "%s", conf->sensors[i].name);
				buf[13]=0;
				spilcdWriteString(&lcd, x+4, y + i*17 + 16 + 8, buf, 0, b, FONT_6x8, DRAW_TO_LCD);
			}

			spilcdWriteString(&lcd, 4, 230,
					"FanPico-" FANPICO_MODEL " v" FANPICO_VERSION,
					0, bg_color, FONT_6x8, DRAW_TO_LCD);
		}
	}



	if (lcd.iCurrentWidth >= 480) {
		draw_fields(state, conf, theme_1.fg);
		return;
	}


	/* Fans */
	x = fans_x;
	y = fans_y;
	for (i = 0; i < FAN_COUNT; i++) {
		b = (i % 2 ? box_color : box2_color);
		rpm = state->fan_freq[i] * 60 / conf->fans[i].rpm_factor;
		pwm = state->fan_duty[i];
		if (rpm > 9999)
			rpm = 9999;
		snprintf(buf, sizeof(buf), "%-4.0lf", rpm);
		spilcdWriteString(&lcd, x + 90, y + i*17 + 16, buf, RGB565(0x0000ba), b, FONT_12x16, DRAW_TO_LCD);
		snprintf(buf, sizeof(buf), "%3.0lf", pwm);
		spilcdWriteString(&lcd, x + 140, y + i*17 + 16, buf, RGB565(0x005100), b, FONT_12x16, DRAW_TO_LCD);
	}

	/* MBFans */
	x = mbfans_x;
	y = mbfans_y;
	for (i = 0; i < MBFAN_COUNT; i++) {
		b = (i % 2 ? box_color : box2_color);
		pwm = state->mbfan_duty[i];
		snprintf(buf, sizeof(buf), "%3.0lf", pwm);
		spilcdWriteString(&lcd, x + 85, y + i*17 + 16, buf, RGB565(0x005100), b, FONT_12x16, DRAW_TO_LCD);
	}


	/* Sensors */
	x = sensors_x;
	y = sensors_y;
	for (i = 0; i < SENSOR_COUNT; i++) {
		b = (i % 2 ? box_color : box2_color);
		temp = state->temp[i];
		snprintf(buf, sizeof(buf), "%3.1lf", temp);
		spilcdWriteString(&lcd, x + 73, y + i*17 + 16, buf, RGB565(0x960000), b, FONT_12x16, DRAW_TO_LCD);
	}


	/* IP */
	const char *ip = network_ip();
	if (ip) {
		snprintf(buf, sizeof(buf), "IP: %s", ip);
		spilcdWriteString(&lcd, 4, 221, buf, 0, bg_color, FONT_6x8, DRAW_TO_LCD);
	}

	/* NTP time */
	if (rtc_get_datetime(&t)) {
		snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
			t.year, t.month, t.day, t.hour, t.min, t.sec);
		spilcdWriteString(&lcd, 198, 221, buf, 0, bg_color, FONT_6x8, DRAW_TO_LCD);
	}

	/* uptime */
	{
		uint32_t secs = to_us_since_boot(get_absolute_time()) / 1000000;
		uint32_t mins =  secs / 60;
		uint32_t hours = mins / 60;
		uint32_t days = hours / 24;

		snprintf(buf, sizeof(buf), "Uptime: %3lu days %02lu:%02lu:%02lu",
				days,
				hours % 24,
				mins % 60,
				secs % 60);
		spilcdWriteString(&lcd, 162, 230, buf, 0, bg_color, FONT_6x8, DRAW_TO_LCD);
	}

}

void lcd_display_message(int rows, const char **text_lines)
{
	int screen_rows = lcd.iCurrentHeight / 16;
	int start_row = 0;

	if (!lcd_found)
		return;

	lcd_clear_display();

	if (rows < screen_rows) {
		start_row = (screen_rows - rows) / 2;
	}

	for(int i = 0; i < rows; i++) {
		int row = start_row + i;
		char *text = (char*)text_lines[i];

		if (row >= screen_rows)
			break;

		spilcdWriteString(&lcd, 0, row * 16, (text ? text : ""), 65535, 0, FONT_12x16, DRAW_TO_LCD);
	}
}


/* eod :-) */
