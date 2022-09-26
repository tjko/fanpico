/* syslog.h
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

#ifndef _FANPICO_SYSLOG_H_
#define _FANPICO_SYSLOG_H_

#include "pico/cyw43_arch.h"

#include "log.h"

#define SYSLOG_DEFAULT_PORT 514
#define SYSLOG_MAX_MSG_LEN  256

int syslog_open(const ip_addr_t *server, u16_t port, int facility, const char *hostname);
void syslog_close();
int syslog_msg(int priority, const char *format, ...);


#endif /* _FANPICO_SYSLOG_H_ */
