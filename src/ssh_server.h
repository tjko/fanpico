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

#define MAX_LOGIN_LENGTH 32
#define MAX_PASSWORD_LENGTH 64
#define MAX_LOGIN_FAILURES 3
#define MAX_SERVER_PKEYS 3

typedef enum ssh_connection_state {
	CS_NONE = 0,
	CS_ACCEPT,
	CS_CONNECT,
} ssh_connection_state_t;

typedef struct ssh_ringbuffer {
	uint8_t *buf;
	bool free_buf;
	size_t size;
	size_t free;
	size_t head;
	size_t tail;
} ssh_ringbuffer_t;


typedef struct ssh_server_pkey_t {
	uint8_t *key;
	uint32_t key_len;
	int key_type;
} ssh_server_pkey_t;

typedef struct ssh_server_t {
	struct tcp_pcb *listen;
	struct tcp_pcb *client;
	ssh_connection_state_t cstate;
	WOLFSSH_CTX *ssh_ctx;
	WOLFSSH* ssh;
	ssh_ringbuffer_t rb_tcp_in;
	ssh_ringbuffer_t rb_in;
	ssh_ringbuffer_t rb_out;
	char login[MAX_LOGIN_LENGTH + 1];

	/* Configuration options... set before calling ssg_server_start() */
	uint16_t port;             /* Listen port (default tcp/22) */
	const char *banner;        /* Login banner string to display when connection starts. */
	ssh_server_pkey_t pkeys[MAX_SERVER_PKEYS];  /* server private key(s) */
	void (*log_cb)(int priority, const char *format, ...);
	bool auth_none;
	int (*auth_cb)(void* ctx, const byte *login, word32 login_len,
		const byte *auth, word32 auth_len, int auth_type);
	void *auth_cb_ctx;
} ssh_server_t;



typedef struct ssh_user_auth_entry_t {
	int type;
	const char *username;
	const uint8_t *auth;
	uint32_t auth_len;
} ssh_user_auth_entry_t;


ssh_server_t* ssh_server_init(size_t rxbuf_size, size_t txbuf_size);
bool ssh_server_start(ssh_server_t *st, bool stdio);
void ssh_server_destroy(ssh_server_t *st);
bool ssh_server_client_connected(ssh_server_t *st);
err_t ssh_server_get_client_ip(const ssh_server_t *st, ip_addr_t *ip, uint16_t *port);
const char* ssh_server_connection_state_name(enum ssh_connection_state state);
err_t ssh_server_disconnect_client(ssh_server_t *st);
const char *ssh_server_get_pubkey_hash(const void *pkey, uint32_t pkey_len, char *str_buf, uint32_t str_buf_len);

void ssh_server_log_level(int priority);


#endif /* SSH_SERVER_H */
