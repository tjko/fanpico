/* sshd.c
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
#include <wolfssh/ssh.h>
#ifdef WOLFCRYPT_TEST
#include <wolfcrypt/test/test.h>
#endif
#include <pico-sshd.h>
#include "util_net.h"
#include "fanpico.h"


static const char *ssh_banner = "\r\n"
	"  _____           ____  _\r\n"
	" |  ___|_ _ _ __ |  _ \\(_) ___ ___\r\n"
	" | |_ / _` | '_ \\| |_) | |/ __/ _ \\\r\n"
	" |  _| (_| | | | |  __/| | (_| (_) |\r\n"
	" |_|  \\__,_|_| |_|_|   |_|\\___\\___/\r\n"
	" ...ssh...\r\n\r\n";


static ssh_user_auth_entry_t *ssh_users = NULL;
static ssh_server_t *ssh_srv = NULL;


static int ssh_allow_connection_cb(ip_addr_t *src_ip)
{
	int ret = 0;
	int aclcount = 0;

	if (!src_ip)
		return -1;

	for (int i = 0; i < SSH_MAX_ACL_ENTRIES; i++) {
		const acl_entry_t *acl = &cfg->ssh_acls[i];

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

void sshserver_init()
{
	char *buf = NULL;
	uint32_t buf_size = 0;
	const char *alg_name;
	int pkey_count = 0;
	int i, res;


	if (wolfCrypt_Init()) {
		log_msg(LOG_ERR, "Failed to initialize wolfCrypt library.");
		return;
	}
#ifdef WOLFCRYPT_TEST
	printf("wolfcrypt_test(): %d\n", wolfcrypt_test(NULL));
#endif

	if (wolfSSH_Init() != WS_SUCCESS) {
		log_msg(LOG_ERR, "Failed to initialize wolfSSH library.");
		return;
	}
#ifndef NDEBUG
	wolfSSH_Debugging_ON();
#endif

	/* Create SSH server instance */
	if (!(ssh_srv = ssh_server_new(2048, 8192))) {
		log_msg(LOG_ERR, "Failed to initialize SSH server.");
		return;
	}

	/* Load SSH server private keys */
	i = 0;
	while ((res = ssh_get_pkey(i++, &buf, &buf_size, &alg_name)) >= 0) {
		if (res) {
			if (ssh_server_add_priv_key(ssh_srv,
							WOLFSSH_FORMAT_ASN1,
							(uint8_t*)buf,
							buf_size)) {
				log_msg(LOG_NOTICE, "Failed to add private key: %s",
					alg_name);
			} else {
				pkey_count++;
			}
		}
	}
	if (pkey_count < 1) {
		log_msg(LOG_ERR, "No private key(s) found for SSH server.");
		return;
	}

	/* Setup context for default authentication callback... */
	ssh_users = calloc(SSH_MAX_PUB_KEYS + 1 + 1, sizeof(ssh_user_auth_entry_t));
	if (!ssh_users) {
		log_msg(LOG_ERR, "Not enough memory for SSH server.");
		return;
	}

	/* Add password authenticated user */
	ssh_users[0].type = WOLFSSH_USERAUTH_PASSWORD;
	ssh_users[0].username = cfg->ssh_user;
	ssh_users[0].auth = (uint8_t*)cfg->ssh_pwhash;

	/* Add publickey authenticated users */
	for (int i = 1; i <= SSH_MAX_PUB_KEYS; i++) {
		const struct ssh_public_key *k = &cfg->ssh_pub_keys[i - 1];
		ssh_users[i].type = WOLFSSH_USERAUTH_PUBLICKEY;
		ssh_users[i].username = k->username;
		ssh_users[i].auth = k->pubkey;
		ssh_users[i].auth_len = k->pubkey_size;
	}

	/* Configure other SSH server settings */
	ssh_srv->auth_enabled = cfg->ssh_auth;
	ssh_srv->auth_cb_ctx = (void*)ssh_users;
	ssh_srv->banner = ssh_banner;
	ssh_srv->log_cb = log_msg;
	ssh_srv->allow_connect_cb = ssh_allow_connection_cb;
	if (cfg->ssh_port > 0)
		ssh_srv->port = cfg->ssh_port;
	ssh_srv->max_auth_tries = 6;

	/* Start SSH server */
	if (!ssh_server_start(ssh_srv, true)) {
		log_msg(LOG_ERR, "Failed to start SSH server.");
	}
}


void sshserver_disconnect()
{
	if (!ssh_srv)
		return;

	ssh_server_disconnect_client(ssh_srv);
}


void sshserver_who()
{
	ip_addr_t ip;
	uint16_t port;
	const char *user, *mode;

	if (!ssh_srv)
		return;

	if (ssh_server_client_connected(ssh_srv)) {
		ip_addr_set_zero(&ip);
		port = 0;
		ssh_server_get_client_ip(ssh_srv, &ip, &port);
		user = (strlen((char*)ssh_srv->login) > 0 ? (char*)ssh_srv->login : "<none>");
		mode = "ssh";

		printf("%-8s %-8s %s:%u (%s)\n", user, mode, ipaddr_ntoa(&ip), port,
			ssh_server_connection_state_name(ssh_srv->cstate));
	} else {
		printf("No active SSH connection(s)\n");
	}
}


/* eof :-) */
