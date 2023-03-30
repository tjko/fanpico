/* default-480x320.h
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


const display_field_t theme_default_480x320_bg[] = {
	{ FAN, LABEL, 0, 3,  36, RGB565(0x000000), RGB565(0xffff99), FONT_8x8 },
	{ FAN, LABEL, 1, 3,  56, RGB565(0x000000), RGB565(0xffcc66), FONT_8x8 },
	{ FAN, LABEL, 2, 3,  76, RGB565(0x000000), RGB565(0xff9966), FONT_8x8 },
	{ FAN, LABEL, 3, 3,  96, RGB565(0x000000), RGB565(0xff9900), FONT_8x8 },
	{ FAN, LABEL, 4, 3, 116, RGB565(0x000000), RGB565(0xff9900), FONT_8x8 },
	{ FAN, LABEL, 5, 3, 136, RGB565(0x000000), RGB565(0xff9966), FONT_8x8 },
	{ FAN, LABEL, 6, 3, 156, RGB565(0x000000), RGB565(0xffcc66), FONT_8x8 },
	{ FAN, LABEL, 7, 3, 176, RGB565(0x000000), RGB565(0xffff99), FONT_8x8 },

	{ MBFAN, LABEL, 0, 3, 214, RGB565(0x000000), RGB565(0xcc6699), FONT_8x8 },
	{ MBFAN, LABEL, 1, 3, 234, RGB565(0x000000), RGB565(0xbb6622), FONT_8x8 },
	{ MBFAN, LABEL, 2, 3, 254, RGB565(0x000000), RGB565(0xcc99cc), FONT_8x8 },
	{ MBFAN, LABEL, 3, 3, 274, RGB565(0x000000), RGB565(0xcc6666), FONT_8x8 },

	{ SENSOR, LABEL, 0, 276, 214, RGB565(0x000000), RGB565(0x6688cc), FONT_8x8 },
	{ SENSOR, LABEL, 1, 276, 234, RGB565(0x000000), RGB565(0x9977aa), FONT_8x8 },
	{ SENSOR, LABEL, 2, 276, 254, RGB565(0x000000), RGB565(0x774466), FONT_8x8 },

	{ 0, 0, -1, -1, -1, 0, 0, -1 }
};

const display_field_t theme_default_480x320_fg[] = {
	{ FAN, RPM, 0, 128,  32, RGB565(0xffff99), RGB565(0x000000), FONT_12x16 },
	{ FAN, RPM, 1, 128,  52, RGB565(0xffcc66), RGB565(0x000000), FONT_12x16 },
	{ FAN, RPM, 2, 128,  72, RGB565(0xff9966), RGB565(0x000000), FONT_12x16 },
	{ FAN, RPM, 3, 128,  92, RGB565(0xff9900), RGB565(0x000000), FONT_12x16 },
	{ FAN, RPM, 4, 128, 112, RGB565(0xff9900), RGB565(0x000000), FONT_12x16 },
	{ FAN, RPM, 5, 128, 132, RGB565(0xff9966), RGB565(0x000000), FONT_12x16 },
	{ FAN, RPM, 6, 128, 152, RGB565(0xffcc66), RGB565(0x000000), FONT_12x16 },
	{ FAN, RPM, 7, 128, 172, RGB565(0xffff99), RGB565(0x000000), FONT_12x16 },
	{ FAN, PWM, 0, 200,  32, RGB565(0xffff99), RGB565(0x000000), FONT_12x16 },
	{ FAN, PWM, 1, 200,  52, RGB565(0xffcc66), RGB565(0x000000), FONT_12x16 },
	{ FAN, PWM, 2, 200,  72, RGB565(0xff9966), RGB565(0x000000), FONT_12x16 },
	{ FAN, PWM, 3, 200,  92, RGB565(0xff9900), RGB565(0x000000), FONT_12x16 },
	{ FAN, PWM, 4, 200, 112, RGB565(0xff9900), RGB565(0x000000), FONT_12x16 },
	{ FAN, PWM, 5, 200, 132, RGB565(0xff9966), RGB565(0x000000), FONT_12x16 },
	{ FAN, PWM, 6, 200, 152, RGB565(0xffcc66), RGB565(0x000000), FONT_12x16 },
	{ FAN, PWM, 7, 200, 172, RGB565(0xffff99), RGB565(0x000000), FONT_12x16 },

	{ MBFAN, RPM, 0, 128, 210, RGB565(0xcc6699), RGB565(0x000000), FONT_12x16 },
	{ MBFAN, RPM, 1, 128, 230, RGB565(0xbb6622), RGB565(0x000000), FONT_12x16 },
	{ MBFAN, RPM, 2, 128, 250, RGB565(0xcc99cc), RGB565(0x000000), FONT_12x16 },
	{ MBFAN, RPM, 3, 128, 270, RGB565(0xcc6666), RGB565(0x000000), FONT_12x16 },
	{ MBFAN, PWM, 0, 200, 210, RGB565(0xcc6699), RGB565(0x000000), FONT_12x16 },
	{ MBFAN, PWM, 1, 200, 230, RGB565(0xbb6622), RGB565(0x000000), FONT_12x16 },
	{ MBFAN, PWM, 2, 200, 250, RGB565(0xcc99cc), RGB565(0x000000), FONT_12x16 },
	{ MBFAN, PWM, 3, 200, 270, RGB565(0xcc6666), RGB565(0x000000), FONT_12x16 },

	{ SENSOR, TEMP, 0, 401, 210, RGB565(0x6688cc), RGB565(0x000000), FONT_12x16 },
	{ SENSOR, TEMP, 1, 401, 230, RGB565(0x9977aa), RGB565(0x000000), FONT_12x16 },
	{ SENSOR, TEMP, 2, 401, 250, RGB565(0x774466), RGB565(0x000000), FONT_12x16 },

	{ IP, OTHER, 0, 350, 308, RGB565(0x000000), RGB565(0xff9900), FONT_8x8 },
	{ UPTIME, OTHER, 0, 292, 48, RGB565(0x000000), RGB565(0xcc99cc), FONT_12x16 },
	{ DATE, OTHER, 0, 300, 90, RGB565(0xff9900), RGB565(0x000000), FONT_12x16 },
	{ TIME, OTHER, 0, 300, 118, RGB565(0xffcc66), RGB565(0x000000), FONT_16x32 },

	{ 0, 0, -1, -1, -1, 0, 0, -1 }
};

extern const uint8_t fanpico_theme_default_480x320_bmp[]; /* ptr to embedded background image */

const struct display_theme theme_default_480x320 = {
	theme_default_480x320_bg,
	theme_default_480x320_fg,
	fanpico_theme_default_480x320_bmp,
	"%14s", 14,
	"%14s", 14,
	"%14s", 14,
};
