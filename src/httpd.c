/* httpd.c
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
#include <time.h>
#include <assert.h>
#include "hardware/rtc.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "pico/util/datetime.h"

#include "fanpico.h"



u16_t ssi_handler(const char *tag, char *insert, int insertlen)
{
	size_t printed = 0;

	/* printf("ssi_handler(\"%s\",%lx,%d)\n", tag, (uint32_t)insert, insertlen); */

	if (!strncmp(tag, "datetime", 8)) {
		datetime_t t;
		if (rtc_get_datetime(&t)) {
			printed = snprintf(insert, insertlen, "%04d-%02d-%02d %02d:%02d:%02d",
					t.year, t.month, t.day, t.hour, t.min, t.sec);
		}
	}
	else if (!strncmp(tag, "model", 5)) {
		printed = snprintf(insert, insertlen, "%s", FANPICO_MODEL);
	}
	else if (!strncmp(tag, "version", 5)) {
		printed = snprintf(insert, insertlen, "%s", FANPICO_VERSION);
	}
	else if (!strncmp(tag, "name", 4)) {
		printed = snprintf(insert, insertlen, "%s", cfg->name);
	}

	return printed;
}

