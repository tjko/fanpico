/* command.c
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
#include <string.h>
#include <assert.h>
#include "pico/stdlib.h"
#include "pico/unique_id.h"

#include "fanpico.h"



void process_command(struct fanpico_state *state, struct fanpico_config *config, const char *command)
{
	int i;

	printf(">%s\n", command);

	if (!strncmp(command, "*IDN?", 5)) {
		pico_unique_board_id_t board_id;
		printf("TJKO Industries,FANPICO-0408,");
		pico_get_unique_board_id(&board_id);
		for (i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++)
			printf("%02x", board_id.id[i]);
		printf(",%s\n", FANPICO_VERSION);
	}
}
