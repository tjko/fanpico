/* network.c
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
#include <assert.h>
#include "pico/stdlib.h"
#ifdef LIB_PICO_CYW43_ARCH
#include "pico/cyw43_arch.h"
#endif

#include "fanpico.h"

#ifdef WIFI_SUPPORT
void wifi_init()
{
	uint32_t country_code = CYW43_COUNTRY_WORLDWIDE;
	int res;

	/* If WiFi country is defined in configuratio, use it... */
	if (cfg) {
		char *country = cfg->wifi_country;
		if (strlen(country) > 1) {
			uint rev = 0;
			if (country[2] >= '0' && country[2] <= '9') {
				rev = country[2] - '0';
			}
			country_code = CYW43_COUNTRY(country[0], country[1], rev);
			debug(2, "WiFi country code: %06x\n", country_code);
		}
	}

	if ((res = cyw43_arch_init_with_country(country_code))) {
		printf("WiFi initialization failed: %d\n", res);
	} else {
		uint8_t wifimac[6];

		cyw43_arch_enable_sta_mode();
		res = cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, wifimac);
		if (res == 0) {
			printf("WiFi MAC: ");
			for (int i = 0; i < 6; i++) {
				printf("%02x", wifimac[i]);
				if (i < 5)
					printf(":");
			}
			printf("\n");
		} else {
			printf("WiFi adapter not found!\n");
			cyw43_arch_deinit();
		}
	}
}
#endif /* WIFI_SUPPORT */


void network_init()
{
#ifdef WIFI_SUPPORT
	wifi_init();
#endif
}

