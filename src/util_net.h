/* util_net.h
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

#ifndef _FANPICO_UTIL_NET_H_
#define _FANPICO_UTIL_NET_H_

#include "lwip/ip_addr.h"


const char *mac_address_str(const uint8_t *mac);
int valid_wifi_country(const char *country);
int valid_hostname(const char *name);
void make_netmask(ip_addr_t *nm, uint8_t prefix);


#endif /* _FANPICO_LOG_H_ */
