/* telnetd.c
   Copyright (C) 2024 Timo Kokkonen <tjko@iki.fi>

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
#include "pico/stdio/driver.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "pico_telnetd.h"
#include "pico_telnetd/util.h"
#include "fanpico.h"


static const char *telnet_banner = "\r\n"
	"  _____           ____  _           \r\n"
	" |  ___|_ _ _ __ |  _ \\(_) ___ ___  \r\n"
	" | |_ / _` | '_ \\| |_) | |/ __/ _ \\ \r\n"
	" |  _| (_| | | | |  __/| | (_| (_) |\r\n"
	" |_|  \\__,_|_| |_|_|   |_|\\___\\___/ \r\n"
	"                                     \r\n";

static user_pwhash_entry_t telnet_users[] = {
	{ NULL, NULL },
	{ NULL, NULL }
};

static tcp_server_t *telnet_srv = NULL;

void telnetserver_init()
{
	telnet_srv = telnet_server_init(4096, 10240);
	if (!telnet_srv)
		return;

	telnet_users[0].login = cfg->telnet_user;
	telnet_users[0].hash = cfg->telnet_pwhash;
	telnet_srv->mode = (cfg->telnet_raw_mode ? RAW_MODE : TELNET_MODE);
	if (cfg->telnet_port > 0)
		telnet_srv->port = cfg->telnet_port;
	if (cfg->telnet_auth) {
		telnet_srv->auth_cb = sha512crypt_auth_cb;
		telnet_srv->auth_cb_param = (void*)telnet_users;
	}
	telnet_srv->log_cb = log_msg;
	telnet_srv->banner = telnet_banner;

	telnet_server_start(telnet_srv, true);
}

