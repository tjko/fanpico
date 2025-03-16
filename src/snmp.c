/* snmp.c
   Copyright (C) 2025 Timo Kokkonen <tjko@iki.fi>

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
#include "pico/stdlib.h"
#ifdef LIB_PICO_CYW43_ARCH
#include "pico/cyw43_arch.h"
#include "lwip/dns.h"
#include "lwip/apps/snmp.h"
#include "lwip/apps/snmp_mib2.h"
#if TLS_SUPPORT
#include "lwip/altcp_tls.h"
#endif
#endif

#include "fanpico.h"

#ifdef WIFI_SUPPORT

static const struct snmp_mib *mibs[] = {
	&mib2,
};

void fanpico_snmp_init()
{
	snmp_set_mibs(mibs, LWIP_ARRAYSIZE(mibs));

	snmp_set_community(cfg->snmp_community);
	snmp_set_community_write(cfg->snmp_community_write);

	snmp_mib2_set_sysdescr((uint8_t*)"FanPico-" FANPICO_MODEL " Fan Controller", NULL);
	snmp_mib2_set_syscontact_readonly((const uint8_t*)cfg->snmp_contact, NULL);
	snmp_mib2_set_syslocation_readonly((const uint8_t*)cfg->snmp_location, NULL);
	snmp_mib2_set_sysname_readonly((const uint8_t*)cfg->name, NULL);

	snmp_init();
}

#endif /* WIFI_SUPPORT */
