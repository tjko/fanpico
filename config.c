/* config.c
   Copyright (C) 2021-2022 Timo Kokkonen <tjko@iki.fi>

   SPDX-License-Identifier: GPL-3.0-or-later

   This file is part of FanPico.

   FanPico is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Foobar is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Foobar. If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include "pico/stdlib.h"

#include "pico_hal.h"

#include "fanpico.h"


void read_config()
{
	int res, fd;

	printf("Reading configuration...\n");

	/* Mount flash filesystem... */
	if ((res = pico_mount(false)) < 0) {
		printf("Mount failed: %d (%s)\n", res, pico_errmsg(res));
		printf("Trying to initialize a new filesystem...\n");
		if ((res = pico_mount(true)) < 0) {
			printf("Unable to initialize flash filesystem!\n");
			return;
		} else {
			printf("Filesystem successfully initialized. (%d)\n", res);
		}
	}
	printf("Filesystem mounted OK\n");

	/* Read configuration file... */
	if ((fd = pico_open("fanpico.cfg", LFS_O_RDONLY)) < 0) {
		printf("Cannot open fanpico.cfg: %d (%s)\n", fd, pico_errmsg(fd));
		pico_unmount();
		return;
	}



	pico_unmount();
}
