/* default-320x240.h
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

const display_field_t theme_default_320x240_bg[] = {
	{ FAN, LABEL, 0, 8,  40, RGB565(0x000000), RGB565(0x00aa44), FONT_6x8 },
	{ FAN, LABEL, 1, 8,  57, RGB565(0x000000), RGB565(0x5fd35f), FONT_6x8 },
	{ FAN, LABEL, 2, 8,  74, RGB565(0x000000), RGB565(0x00aa44), FONT_6x8 },
	{ FAN, LABEL, 3, 8,  91, RGB565(0x000000), RGB565(0x5fd35f), FONT_6x8 },
	{ FAN, LABEL, 4, 8, 108, RGB565(0x000000), RGB565(0x00aa44), FONT_6x8 },
	{ FAN, LABEL, 5, 8, 125, RGB565(0x000000), RGB565(0x5fd35f), FONT_6x8 },
	{ FAN, LABEL, 6, 8, 142, RGB565(0x000000), RGB565(0x00aa44), FONT_6x8 },
	{ FAN, LABEL, 7, 8, 159, RGB565(0x000000), RGB565(0x5fd35f), FONT_6x8 },

	{ MBFAN, LABEL, 0, 193, 40, RGB565(0x000000), RGB565(0x00aa44), FONT_6x8 },
	{ MBFAN, LABEL, 1, 193, 57, RGB565(0x000000), RGB565(0x5fd35f), FONT_6x8 },
	{ MBFAN, LABEL, 2, 193, 74, RGB565(0x000000), RGB565(0x00aa44), FONT_6x8 },
	{ MBFAN, LABEL, 3, 193, 91, RGB565(0x000000), RGB565(0x5fd35f), FONT_6x8 },

	{ SENSOR, LABEL, 0, 193, 150, RGB565(0x000000), RGB565(0x00aa44), FONT_6x8 },
	{ SENSOR, LABEL, 1, 193, 167, RGB565(0x000000), RGB565(0x5fd35f), FONT_6x8 },
	{ SENSOR, LABEL, 2, 193, 184, RGB565(0x000000), RGB565(0x00aa44), FONT_6x8 },

	{ MODEL_VERSION, LABEL, 0, 12, 230, RGB565(0x000000), RGB565(0x008080), FONT_6x8 },

	{ 0, 0, -1, -1, -1, 0, 0, -1 }
};

const display_field_t theme_default_320x240_fg[] = {
	{ FAN, RPM, 0,  94,  36, RGB565(0xccff00), RGB565(0x00aa44), FONT_12x16 },
	{ FAN, RPM, 1,  94,  53, RGB565(0xccff00), RGB565(0x5fd35f), FONT_12x16 },
	{ FAN, RPM, 2,  94,  70, RGB565(0xccff00), RGB565(0x00aa44), FONT_12x16 },
	{ FAN, RPM, 3,  94,  87, RGB565(0xccff00), RGB565(0x5fd35f), FONT_12x16 },
	{ FAN, RPM, 4,  94, 104, RGB565(0xccff00), RGB565(0x00aa44), FONT_12x16 },
	{ FAN, RPM, 5,  94, 121, RGB565(0xccff00), RGB565(0x5fd35f), FONT_12x16 },
	{ FAN, RPM, 6,  94, 138, RGB565(0xccff00), RGB565(0x00aa44), FONT_12x16 },
	{ FAN, RPM, 7,  94, 155, RGB565(0xccff00), RGB565(0x5fd35f), FONT_12x16 },
	{ FAN, PWM, 0, 144,  36, RGB565(0x0055d4), RGB565(0x00aa44), FONT_12x16 },
	{ FAN, PWM, 1, 144,  53, RGB565(0x0055d4), RGB565(0x5fd35f), FONT_12x16 },
	{ FAN, PWM, 2, 144,  70, RGB565(0x0055d4), RGB565(0x00aa44), FONT_12x16 },
	{ FAN, PWM, 3, 144,  87, RGB565(0x0055d4), RGB565(0x5fd35f), FONT_12x16 },
	{ FAN, PWM, 4, 144, 104, RGB565(0x0055d4), RGB565(0x00aa44), FONT_12x16 },
	{ FAN, PWM, 5, 144, 121, RGB565(0x0055d4), RGB565(0x5fd35f), FONT_12x16 },
	{ FAN, PWM, 6, 144, 138, RGB565(0x0055d4), RGB565(0x00aa44), FONT_12x16 },
	{ FAN, PWM, 7, 144, 155, RGB565(0x0055d4), RGB565(0x5fd35f), FONT_12x16 },

	{ MBFAN, PWM, 0, 274,  36, RGB565(0xffcc00), RGB565(0x00aa44), FONT_12x16 },
	{ MBFAN, PWM, 1, 274,  53, RGB565(0xffcc00), RGB565(0x5fd35f), FONT_12x16 },
	{ MBFAN, PWM, 2, 274,  70, RGB565(0xffcc00), RGB565(0x00aa44), FONT_12x16 },
	{ MBFAN, PWM, 3, 274,  87, RGB565(0xffcc00), RGB565(0x5fd35f), FONT_12x16 },

	{ SENSOR, TEMP, 0, 262, 146, RGB565(0xff5599), RGB565(0x00aa44), FONT_12x16 },
	{ SENSOR, TEMP, 1, 262, 163, RGB565(0xff5599), RGB565(0x5fd35f), FONT_12x16 },
	{ SENSOR, TEMP, 2, 262, 180, RGB565(0xff5599), RGB565(0x00aa44), FONT_12x16 },

	{   TIME, OTHER, 0,  34, 192, RGB565(0x000000), RGB565(0x008080), FONT_16x32 },
//	{   DATE, OTHER, 0,  36, 207, RGB565(0x000000), RGB565(0x008080), FONT_12x16 },
	{     IP, OTHER, 0, 216, 220, RGB565(0x000000), RGB565(0x008080), FONT_6x8 },
	{ UPTIME, OTHER, 0, 234, 230, RGB565(0x000000), RGB565(0x008080), FONT_6x8 },

	{ 0, 0, -1, -1, -1, 0, 0, -1 }
};

extern const uint8_t fanpico_theme_default_320x240_bmp[]; /* ptr to embedded background image */

const struct display_theme theme_default_320x240 = {
	theme_default_320x240_bg,
	theme_default_320x240_fg,
	fanpico_theme_default_320x240_bmp,
	"%-14s", 14,
	"%-13s", 13,
	"%-11s", 11,
};
