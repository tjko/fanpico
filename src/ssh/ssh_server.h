/* ssh_server.h
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

#ifndef SSH_SERVER_H
#define SSH_SERVER_H 1

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "lwip/tcp.h"
#include <wolfssh/ssh.h>

#include "ssh/ringbuffer.h"

#define MAX_LOGIN_LENGTH 32
#define MAX_SERVER_PKEYS 3


/* Client connection state */
typedef enum ssh_connection_state {
	CS_NONE = 0,
	CS_ACCEPT,
	CS_CONNECT,
} ssh_connection_state_t;

/* SSH Server Private Key */
typedef struct ssh_server_pkey_t {
	const uint8_t *key;
	uint32_t key_len;
	int key_type;
} ssh_server_pkey_t;

/* SSH Server Instance */
typedef struct ssh_server_t {
	struct tcp_pcb *listen;            /* lwIP listenig socket */
	struct tcp_pcb *client;            /* lwIP client connection socket */
	ssh_connection_state_t cstate;     /* Current client connection state */
	WOLFSSH_CTX *ssh_ctx;              /* SSH server context */
	WOLFSSH* ssh;                      /* SSH (client) connection context */
	ssh_ringbuffer_t rb_tcp_in;        /* TCP (encrypted) input buffer */
	ssh_ringbuffer_t rb_in;            /* Decrypted input buffer */
	ssh_ringbuffer_t rb_out;           /* Unencrypted output buffer */
	char login[MAX_LOGIN_LENGTH + 1];  /* Currently logged in user */

	/* Configuration options... set before calling ssg_server_start() */
	uint16_t port;             /* Listen port (default tcp/22) */
	const char *banner;        /* Login banner string to display when connection starts. */
	ssh_server_pkey_t pkeys[MAX_SERVER_PKEYS];  /* server private key(s) */
	bool auth_enabled;         /* Set false to disable authentication. */

	/* Callback functions */
	int (*auth_cb)(void* ctx, const byte *login, word32 login_len,
		const byte *auth, word32 auth_len, int auth_type);
	void *auth_cb_ctx;
	void (*log_cb)(int priority, const char *format, ...);
} ssh_server_t;

/* Authentication entry used by default authentication callback routine. */
typedef struct ssh_user_auth_entry_t {
	int type;              /* Entry type:
				    WOLFSSH_USERAUTH_PASSWORD,
				    WOLFSSH_USERAUTH_PUBLICKEY
			       */
	const char *username;  /* User name (string) */
	const uint8_t *auth;   /* Password hash (string) or public key (binary) */
	uint32_t auth_len;     /* Needed when type is WOLFSSH_USERAUTH_PUBLICKEY */
} ssh_user_auth_entry_t;



/* ssh_server.c */
ssh_server_t* ssh_server_new(size_t rxbuf_size, size_t txbuf_size);
bool ssh_server_start(ssh_server_t *st, bool stdio);
void ssh_server_destroy(ssh_server_t *st);
bool ssh_server_client_connected(ssh_server_t *st);
err_t ssh_server_get_client_ip(const ssh_server_t *st, ip_addr_t *ip, uint16_t *port);
const char* ssh_server_connection_state_name(enum ssh_connection_state state);
err_t ssh_server_disconnect_client(ssh_server_t *st);
const char *ssh_server_get_pubkey_hash(const void *pkey, uint32_t pkey_len, char *str_buf, uint32_t str_buf_len);
void ssh_server_log_level(int priority);
int ssh_server_add_priv_key(ssh_server_t *st, int type, const uint8_t *key, uint32_t key_len);

/* sha512crypt.c */
char *ssh_server_sha512_crypt (const char *key, const char *salt);

#endif /* SSH_SERVER_H */
