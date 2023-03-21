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
#include "pico/stdlib.h"

#include "fanpico.h"


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


/* eof :-) */
