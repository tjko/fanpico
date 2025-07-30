/* util_net.c
   Copyright (C) 2021-2025 Timo Kokkonen <tjko@iki.fi>

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
#include "lwip/ip_addr.h"

#include "fanpico.h"



const char *mac_address_str(const uint8_t *mac)
{
	static char buf[32];

	snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	return buf;
}


int valid_wifi_country(const char *country)
{
	if (!country)
		return 0;

	if (strlen(country) < 2)
		return 0;

	if (!(country[0] >= 'A' && country[0] <= 'Z'))
		return 0;
	if (!(country[1] >= 'A' && country[1] <= 'Z'))
		return 0;

	if (strlen(country) == 2)
		return 1;

	if (country[2] >= '1' && country[2] <= '9')
		return 1;

	return 0;
}


int valid_hostname(const char *name)
{
	if (!name)
		return 0;

	for (int i = 0; i < strlen(name); i++) {
		if (!(isalnum((int)name[i]) || name[i] == '-')) {
			return 0;
		}
	}

	return 1;
}


void make_netmask(ip_addr_t *nm, uint8_t prefix)
{
	uint32_t mask = 0xffffffff;

	if (prefix < 32)
		mask <<= (32 - prefix);

	if (nm)
		ip4_addr_set_u32(nm, lwip_htonl(mask));
}


/* eof */
