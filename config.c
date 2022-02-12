/* config.c
   Copyright (C) 2021-2022 Timo Kokkonen <tjko@iki.fi>

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
#include <malloc.h>
#include "pico/stdlib.h"
#include "cJSON.h"
#include "pico_hal.h"

#include "fanpico.h"

/* default_config.s */
extern const char fanpico_default_config[];

cJSON *config = NULL;

void print_mallinfo()
{
	printf("mallinfo:\n");
	printf("  arena: %u\n", mallinfo().arena);
	printf(" ordblks: %u\n", mallinfo().ordblks);
	printf("uordblks: %u\n", mallinfo().uordblks);
	printf("fordblks: %u\n", mallinfo().fordblks);
}

void read_config()
{
	int res, fd;

	const char *default_config = fanpico_default_config;
	uint32_t default_config_size = strlen(default_config);

	printf("Reading configuration...\n");

	printf("config size = %lu\n", default_config_size);
//	printf("default config:\n---\n%s\n---\n", default_config);

	/* Mount flash filesystem... */
	if ((res = pico_mount(false)) < 0) {
		printf("Mount failed: %d (%s)\n", res, pico_errmsg(res));
		printf("Trying to initialize a new filesystem...\n");
		if ((res = pico_mount(true)) < 0) {
			printf("Unable to initialize flash filesystem!\n");
		} else {
			printf("Filesystem successfully initialized. (%d)\n", res);
		}
	} else {
		printf("Filesystem mounted OK\n");
		/* Read configuration file... */
		if ((fd = pico_open("fanpico.cfg", LFS_O_RDONLY)) < 0) {
			printf("Cannot open fanpico.cfg: %d (%s)\n", fd, pico_errmsg(fd));
		} else {
			// read saved config...
		}
	}
	pico_unmount();

	print_mallinfo();
	if (!config) {
		printf("Using default configuration...\n");
		config = cJSON_Parse(fanpico_default_config);
		if (!config) {
			const char *error_str = cJSON_GetErrorPtr();
			panic("Failed to parse default config: %s\n",
				(error_str ? error_str : "") );
		}
	}
	print_mallinfo();
	cJSON *id;

	id = cJSON_GetObjectItem(config, "id");
	if (id)
		printf("config version: %s\n", id->valuestring);


	uint8_t *buf = malloc(64*1024);
	if (!buf)
		printf("malloc failed\n");

	print_mallinfo();

	cJSON_Delete(config);
	print_mallinfo();
	sleep_ms(10000);

}
