/* display.c
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

#include "fanpico.h"

#if LCD_DISPLAY
#include "bb_spi_lcd.h"
#endif
#if OLED_DISPLAY
#include "ss_oled.h"
#endif


#if LCD_DISPLAY
static SPILCD lcd;
static uint8_t lcd_found = 0;

#define LCD_LOGO_WIDTH 160
#define LCD_LOGO_HEIGHT 140
extern uint8_t fanpico_lcd_logo_bmp[]; /* ptr to embedded lcd-logo.bmp */

/* Macros for converting RGB888 colorspace to RGB565 */

/* Convert 24bit color space to 16bit color space.  */
#define RGB565(rgb) (uint16_t)( ((rgb & 0xf80000) >> 8) | ((rgb & 0xfc00) >> 5) | ((rgb & 0xf8) >> 3) )
/* Generate 16bit color from 8bit RGB components. */
#define RGB565C(r, g, b) (uint16_t)( ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | ((b & 0xf8) >> 3) )

static uint16_t bg_color = RGB565(0x003531);  // 0x006567
static uint16_t box_color = RGB565(0x008e4a);
static uint16_t box2_color = RGB565(0x049467);
static uint16_t text_color = RGB565(0x07a3aa);


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
	int32_t spi_freq = (48 * 1024 * 1024);
	int val;


	char *args, *tok, *saveptr;

	if (!cfg)
		return;

	args = strdup(cfg->display_type);
	if (!args)
		return;
	tok = strtok_r(args, ",", &saveptr);
	while (tok) {
		if (!strncmp(tok,"lcd=", 4)) {
			char *t = tok + 4;
			if (!strncmp(t, "ILI9341", 7))
				dtype = LCD_ILI9341;
			else if (!strncmp(t, "ILI9342", 7))
				dtype = LCD_ILI9342;
			else if (!strncmp(t, "ILI9486", 7))
				dtype = LCD_ILI9486;
			else if (!strncmp(t, "ST7789", 6))
				dtype = LCD_ST7789;
			else if (!strncmp(t, "HX8357", 6))
				dtype = LCD_HX8357;
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
		else if (!strncmp(tok, "invert", 6))
			flags |= FLAGS_INVERT;

		tok = strtok_r(NULL, ",", &saveptr);
	}
	free(args);

	if (dtype == LCD_INVALID) {
		log_msg(LOG_NOTICE, "No LCD type configured!");
		return;
	}
	lcd_found = 1;

	log_msg(LOG_NOTICE, "Initializing LCD display...");

	memset(&lcd, 0, sizeof(lcd));
	int res = spilcdInit(&lcd, dtype, FLAGS_NONE, spi_freq,
		CS_PIN, DC_PIN, LCD_RESET_PIN, LCD_LIGHT_PIN,
		MISO_PIN, MOSI_PIN, SCK_PIN);
	log_msg(LOG_DEBUG, "spilcdInit(): %d", res);

	spilcdSetOrientation(&lcd, orientation);
	spilcdFill(&lcd, 0, DRAW_TO_LCD);

	log_msg(LOG_NOTICE, "LCD display: %dx%d (native: %dx%d)",
		lcd.iCurrentWidth, lcd.iCurrentHeight,
		lcd.iWidth, lcd.iHeight);

	spilcdDrawBMP(&lcd, fanpico_lcd_logo_bmp,
		(lcd.iCurrentWidth - LCD_LOGO_WIDTH) / 2,
		10,
		0, -1, DRAW_TO_LCD);
	spilcdWriteString(&lcd, 60, 165, "FanPico-" FANPICO_MODEL, text_color, 0, FONT_16x16, DRAW_TO_LCD);
	spilcdWriteString(&lcd, 120, 190, "v" FANPICO_VERSION, text_color, 0, FONT_16x16, DRAW_TO_LCD);
	spilcdWriteString(&lcd, 110, 220, "Initializing...", text_color, 0, FONT_8x8, DRAW_TO_LCD);
}

void lcd_clear_display()
{
	if (!lcd_found)
		return;

	spilcdFill(&lcd, 0, DRAW_TO_LCD);
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
		spilcdFill(&lcd, 0x0000, DRAW_TO_LCD);
		if (bg_color != 0)
			draw_rounded_box(&lcd, o_x + 0, o_y + 0, 320, 240, 10, bg_color);

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
		bg_drawn = 1;
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
#endif /* LCD_DISPLAY */

#if OLED_DISPLAY
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

	oled_found = 1;

	oledSetBackBuffer(&oled, ucBuffer);
	oledFill(&oled, 0, 1);
	oledSetContrast(&oled, disp_brightness);
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
			oledWriteString(&oled, 0, 0, 0, "Fans", FONT_6x8, 0, 1);
			oledWriteString(&oled, 0, 74, 0, "MB Inputs", FONT_6x8, 0, 1);
			oledWriteString(&oled, 0, 76, 6, "Sensors", FONT_6x8, 0, 1);
			oledDrawLine(&oled, 72, 44, oled_width - 1, 44, 1);
			oledDrawLine(&oled, 72, 0, 72, 80, 1);
		} else {
			//oledWriteString(&oled, 0, 74, 0, "mb inputs", FONT_6x8, 0, 1);
			oledDrawLine(&oled, 70, 35, oled_width - 1, 35, 1);
			oledDrawLine(&oled, 70, 0, 70, 63, 1);
		}
		bg_drawn = 1;
	}

	if (oled_height <= 64) {
		for (i = 0; i < FAN_COUNT; i++) {
			rpm = state->fan_freq[i] * 60 / conf->fans[i].rpm_factor;
			pwm = state->fan_duty[i];
			snprintf(buf, sizeof(buf), "%d:%4.0lf %3.0lf%%", i + 0, rpm, pwm);
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

		//oledDrawLine(&oled, 68, 40-1, oled_width - 1, 40-1, 1);
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
			snprintf(buf, sizeof(buf), "IP: %s", ip);
			oledWriteString(&oled, 0, 12, 15, buf, FONT_6x8, 0, 1);
		}

		if (rtc_get_datetime(&t)) {
			/* NTP time */
			snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
				t.year, t.month, t.day, t.hour, t.min, t.sec);
			oledWriteString(&oled, 0, 6, 13, buf, FONT_6x8, 0, 1);
		}
		else {
			/* uptime */

			uint32_t secs = to_us_since_boot(get_absolute_time()) / 1000000;
			uint32_t mins =  secs / 60;
			uint32_t hours = mins / 60;
			uint32_t days = hours / 24;

			snprintf(buf, sizeof(buf), "%3lu days %02lu:%02lu:%02lu",
				days,
				hours % 24,
				mins % 60,
				secs % 60);
			oledWriteString(&oled, 0, 6, 13, buf, FONT_6x8, 0, 1);
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

#endif /* OLED_DISPLAY */


void display_init()
{
#if OLED_DISPLAY || LCD_DISPLAY
#if OLED_DISPLAY
	if (!cfg->spi_active)
		oled_display_init();
#endif
#if LCD_DISPLAY
	if (cfg->spi_active)
		lcd_display_init();
#endif
#else
	log_msg(LOG_NOTICE, "No Display Support");
#endif
}

void clear_display()
{
#if LCD_DISPLAY
	if (cfg->spi_active)
		lcd_clear_display();
#endif
#if OLED_DISPLAY
	if (!cfg->spi_active)
		oled_clear_display();
#endif
}

void display_status(const struct fanpico_state *state,
	const struct fanpico_config *config)
{
#if LCD_DISPLAY
	if (cfg->spi_active)
		lcd_display_status(state, config);
#endif
#if OLED_DISPLAY
	if (!cfg->spi_active)
		oled_display_status(state, config);
#endif
}

void display_message(int rows, const char **text_lines)
{
#if LCD_DISPLAY
	if (cfg->spi_active)
		lcd_display_message(rows, text_lines);
#endif
#if OLED_DISPLAY
	if (!cfg->spi_active)
		oled_display_message(rows, text_lines);
#endif
}


