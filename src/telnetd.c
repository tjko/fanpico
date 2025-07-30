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
#include "pico/stdlib.h"
#include "pico/stdio/driver.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "pico_telnetd.h"
#include "pico_telnetd/util.h"
#include "util_net.h"
#include "fanpico.h"


static const char *telnet_banner = "\r\n"
	"  _____           ____  _\r\n"
	" |  ___|_ _ _ __ |  _ \\(_) ___ ___\r\n"
	" | |_ / _` | '_ \\| |_) | |/ __/ _ \\\r\n"
	" |  _| (_| | | | |  __/| | (_| (_) |\r\n"
	" |_|  \\__,_|_| |_|_|   |_|\\___\\___/\r\n"
	"\r\n";

static user_pwhash_entry_t telnet_users[] = {
	{ NULL, NULL },
	{ NULL, NULL }
};

static tcp_server_t *telnet_srv = NULL;


static int telnet_allow_connection_cb(ip_addr_t *src_ip)
{
	int ret = 0;
	int aclcount = 0;

	if (!src_ip)
		return -1;

	for (int i = 0; i < TELNET_MAX_ACL_ENTRIES; i++) {
		const acl_entry_t *acl = &cfg->telnet_acls[i];

		if (acl->prefix > 0) {
			ip_addr_t netmask;

			aclcount++;
			make_netmask(&netmask, acl->prefix);
			if (ip_addr_netcmp(&acl->ip, src_ip, &netmask)) {
				ret = 1;
				break;
			}
		}
	}

	return (aclcount > 0 ? ret: 1);
}


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
	telnet_srv->allow_connect_cb = telnet_allow_connection_cb;

	telnet_server_start(telnet_srv, true);
}


void telnetserver_disconnect()
{
	if (!telnet_srv)
		return;

	telnet_server_disconnect_client(telnet_srv);
}


void telnetserver_who()
{
	ip_addr_t ip;
	uint16_t port;
	const char *user, *mode;

	if (!telnet_srv)
		return;

	if (telnet_server_client_connected(telnet_srv)) {
		ip_addr_set_zero(&ip);
		port = 0;
		telnet_server_get_client_ip(telnet_srv, &ip, &port);
		user = (strlen((char*)telnet_srv->login) > 0 ? (char*)telnet_srv->login : "<none>");
		mode = (telnet_srv->mode == TELNET_MODE ? "telnet" : "tcp");

		printf("%-8s %-8s %s:%u (%s)\n", user, mode, ipaddr_ntoa(&ip), port,
			tcp_connection_state_name(telnet_srv->cstate));
	} else {
		printf("No active telnet connection(s)\n");
	}
}
