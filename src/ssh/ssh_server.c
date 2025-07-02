/* ssh_server.c
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
#include "pico/stdlib.h"
#include "pico/stdio/driver.h"
#include "pico/cyw43_arch.h"
#include "pico/aon_timer.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "wolfssl/wolfcrypt/settings.h"
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/coding.h>
#include <wolfssl/ssl.h>
#include <wolfssh/ssh.h>

#include "ssh/ssh_server.h"


#define SSH_DEFAULT_PORT 22
#define SSH_SERVER_MAX_CONN 1
#define SSH_CLIENT_POLL_TIME 1


#define LOG_MAX_MSG_LEN 256

#define LOG_EMERG     0
#define LOG_ALERT     1
#define LOG_CRIT      2
#define LOG_ERR       3
#define LOG_WARNING   4
#define LOG_NOTICE    5
#define LOG_INFO      6
#define LOG_DEBUG     7

static int global_log_level = LOG_ERR;

static ssh_server_t *stdio_sshserv = NULL;
static void (*chars_available_callback)(void*) = NULL;
static void *chars_available_param = NULL;

static const char *ssh_default_banner = "\r\npico-sshd\r\n\r\n";
static const char* connected_text = "Connected.\r\n";

#ifndef LOG_MSG
#define LOG_MSG(...) { if (st) if (st->log_cb) st->log_cb(__VA_ARGS__); }
#endif



static void ssh_server_log_msg(int priority, const char *format, ...)
{
	va_list ap;
	char *buf;
	char tstamp[32];
	int len;
	uint core = get_core_num();

	if (priority > global_log_level)
		return;

	if (!(buf = malloc(LOG_MAX_MSG_LEN)))
		return;

	va_start(ap, format);
	vsnprintf(buf, LOG_MAX_MSG_LEN, format, ap);
	va_end(ap);

	if ((len = strnlen(buf, LOG_MAX_MSG_LEN - 1)) > 0) {
		/* If string ends with \n, remove it. */
		if (buf[len - 1] == '\n')
			buf[len - 1] = 0;
	}


	uint64_t t = to_us_since_boot(get_absolute_time());
	snprintf(tstamp, sizeof(tstamp), "[%6llu.%06llu][%u]",
		(t / 1000000), (t % 1000000), core);
	printf("%s %s %s\n", tstamp, "SSH", buf);

	free(buf);
}


static int ssh_server_default_auth_cb(void *ctx, const byte *login, word32 login_len,
				const byte *auth, word32 auth_len, int auth_type)
{
	ssh_user_auth_entry_t *users = (ssh_user_auth_entry_t*)ctx;
	char *pw, *hash;
	int i = 0;
	int res = 2;


	if (!ctx || !login || !auth)
		return res;

	/* Go through list of authentication entries */
	while (users[i].username) {
		ssh_user_auth_entry_t *u = &users[i++];

		if (!u->auth || auth_type != u->type)
			continue;
		if (strlen(u->username) < 1)
			continue;
		if (strcasecmp((const char*)login, u->username))
			continue;

		if (auth_type == WOLFSSH_USERAUTH_PASSWORD) {
			hash = NULL;
			if (strlen((char*)u->auth) > 0) {
				if ((pw = malloc(auth_len + 1))) {
					memcpy(pw, auth, auth_len);
					pw[auth_len] = 0;
					hash = ssh_server_sha512_crypt(pw, (const char*)u->auth);
					memset(pw, 0, auth_len);
					free(pw);
				}
			}
			if (hash) {
				if (!strcmp(hash, (char*)u->auth)) {
					/* password matches */
					res = 0;
					break;
				} else {
					/* password does not match */
					res = 1;
					break;
				}
			}
		}
		else if (auth_type == WOLFSSH_USERAUTH_PUBLICKEY) {
			if (!memcmp(auth, u->auth, auth_len)) {
				/* public key matches */
				res = 0;
				break;
			}
		}
	}

	return res;
}


ssh_server_t* ssh_server_new(size_t rxbuf_size, size_t txbuf_size)
{
	ssh_server_t *st = calloc(1, sizeof(ssh_server_t));

	if (!st)
		return NULL;

	st->cstate = CS_NONE;
	st->log_cb = ssh_server_log_msg;
	st->auth_enabled = true;
	st->auth_cb = ssh_server_default_auth_cb;
	st->port = SSH_DEFAULT_PORT;
	st->banner = ssh_default_banner;

	ssh_ringbuffer_init(&st->rb_tcp_in, NULL, rxbuf_size);
	ssh_ringbuffer_init(&st->rb_in, NULL, rxbuf_size);
	ssh_ringbuffer_init(&st->rb_out, NULL, txbuf_size);

	return st;
}


#if 0
static err_t ssh_server_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
	ssh_server_t *st = (ssh_server_t*)arg;

	LOG_MSG(LOG_DEBUG, "ssh_server_sent: %u", len);
	return ERR_OK;
}
#endif


static err_t close_client_connection(struct tcp_pcb *pcb)
{
	err_t err = ERR_OK;

	tcp_arg(pcb, NULL);
	tcp_sent(pcb, NULL);
	tcp_recv(pcb, NULL);
	tcp_err(pcb, NULL);
	tcp_poll(pcb, NULL, 0);

	if ((err = tcp_close(pcb)) != ERR_OK) {
		tcp_abort(pcb);
		err = ERR_ABRT;
	}

	return err;
}


static err_t ssh_close_client_connection(ssh_server_t *st)
{
	err_t err = ERR_OK;

	st->cstate = CS_NONE;
	st->login[0] = 0;
	if (st->client) {
		err = close_client_connection(st->client);
		st->client = NULL;
	}
	if (st->ssh) {
		wolfSSH_free(st->ssh);
		st->ssh = NULL;
	}

	return err;
}


static err_t ssh_process_input(ssh_server_t *st)
{
	int res, err;
	uint8_t tmp[64];
	word32 channel_id = 0;


	res = wolfSSH_worker(st->ssh, &channel_id);
	if (res != WS_CHAN_RXD)
		return ERR_OK;


	channel_id = 0;

	do {
		if (ssh_ringbuffer_free(&st->rb_in) >= sizeof(tmp)) {
			res = wolfSSH_ChannelIdRead(st->ssh, channel_id, tmp, sizeof(tmp));
			err = wolfSSH_get_error(st->ssh);
			if (res > 0) {
				if (channel_id == 0)
					ssh_ringbuffer_add(&st->rb_in, tmp, res, false);
			}
			else if (res < 0 && (err == WS_WANT_READ || err == WS_WANT_WRITE)) {
				/* no more data to read... */
			}
			else {
				//LOG_MSG(LOG_INFO, "wolfSSH_stream_read(): error %d (%d)", res, err);
				//ssh_close_client_connection(st);
			}
		} else {
			res = 0;
		}
	} while (res > 0);

	if (ssh_ringbuffer_used(&st->rb_in) > 0) {
		if (st->cstate == CS_CONNECT && chars_available_callback) {
			chars_available_callback(chars_available_param);
		}
	}

	return ERR_OK;
}


static err_t ssh_server_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
	ssh_server_t *st = (ssh_server_t*)arg;
	struct pbuf *buf;
	size_t len;


	if (!p) {
		/* Connection closed by client */
		LOG_MSG(LOG_INFO, "SSH Client closed connection: %s:%u (%d)",
			ip4addr_ntoa(&pcb->remote_ip), pcb->remote_port, err);
		ssh_close_client_connection(st);
		return ERR_OK;
	}
	if (err != ERR_OK) {
		/* unknown error... */
		LOG_MSG(LOG_WARNING, "ssh_server_recv: error received: %d", err);
		if (p)
			pbuf_free(p);
		return err;
	}

	/* Read as much data as we can fit in the ringbuffer... */
	while (p != NULL && p->len <= ssh_ringbuffer_free(&st->rb_tcp_in)) {
		buf = p;
		if (ssh_ringbuffer_add(&st->rb_tcp_in, buf->payload, buf->len, false))
			break;

		u16_t p_len = buf->len;
		p = buf->next;
		if (p)
			pbuf_ref(p);

		u8_t freed;
		do {
			freed = pbuf_free(buf);
		} while (freed == 0);
		tcp_recved(pcb, p_len);
	}

	if ((len = ssh_ringbuffer_used(&st->rb_tcp_in)) > 0) {
		if (st->cstate == CS_CONNECT)
			ssh_process_input(st);
	}

	return ERR_OK;
}


static int ssh_server_flush_buffer(ssh_server_t *st)
{
	void *rbuf;
	int waiting;
	int wcount = 0;
	word32 channel_id = 0;


	if (!st)
		return -1;
	if (st->cstate != CS_CONNECT) {
		ssh_ringbuffer_flush(&st->rb_out);
		return 0;
	}

	while ((waiting = ssh_ringbuffer_used(&st->rb_out)) > 0) {
		size_t len = ssh_ringbuffer_peek(&st->rb_out, &rbuf, waiting);
		if (len > 0) {
			int res = wolfSSH_ChannelIdSend(st->ssh, channel_id, rbuf, len);
			if (res != len)
				break;
			ssh_ringbuffer_read(&st->rb_out, NULL, len);
			wcount++;
		} else {
			break;
		}
	}

	return wcount;
}



static err_t ssh_server_poll(void *arg, struct tcp_pcb *pcb)
{
	ssh_server_t *st = (ssh_server_t*)arg;
	int res, err;

	if (st->cstate == CS_ACCEPT) {
		if ((res = wolfSSH_accept(st->ssh)) == WS_SUCCESS) {
			LOG_MSG(LOG_DEBUG, "wolfSSH_accept(): ok");
			st->cstate = CS_CONNECT;
			strncpy(st->login, wolfSSH_GetUsername(st->ssh), sizeof(st->login) - 1);
			st->login[sizeof(st->login) - 1] = 0;
			ssh_ringbuffer_add(&st->rb_out, connected_text, strlen(connected_text), true);
		} else {
			err = wolfSSH_get_error(st->ssh);
			if (err == WS_WANT_READ || err == WS_WANT_WRITE)
				return ERR_OK;
			LOG_MSG(LOG_INFO, "wolfSSH_accept(): failed: %d (%d)", err, res);
			ssh_close_client_connection(st);
		}
	}
	else if (st->cstate == CS_CONNECT) {
		ssh_process_input(st);
		ssh_server_flush_buffer(st);
	}

	return ERR_OK;
}


static void ssh_server_err(void *arg, err_t err)
{
	ssh_server_t *st = (ssh_server_t*)arg;

	if (err == ERR_ABRT)
		return;

	LOG_MSG(LOG_ERR,"ssh_server_err: client connection error: %d", err);
}


static err_t ssh_server_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
	ssh_server_t *st = (ssh_server_t*)arg;
	WOLFSSH *ssh = NULL;

	if (!pcb || err != ERR_OK) {
		LOG_MSG(LOG_ERR, "ssh_server_accept: failure: %d", err);
		return ERR_VAL;
	}

	LOG_MSG(LOG_INFO, "SSH Client connected: %s:%u",
		ip4addr_ntoa(&pcb->remote_ip), pcb->remote_port);

	if (st->cstate != CS_NONE) {
		LOG_MSG(LOG_ERR, "ssh_server_accept: reject connection");
		return ERR_MEM;
	}

	if (!(ssh = wolfSSH_new(st->ssh_ctx))) {
		LOG_MSG(LOG_ERR, "ssh_server_accept: failed to create SSH session");
		return ERR_MEM;
	}
	wolfSSH_SetUserAuthCtx(ssh, st);
	wolfSSH_SetUserAuthResultCtx(ssh, st);
	wolfSSH_SetIOReadCtx(ssh, st);
	wolfSSH_SetIOWriteCtx(ssh, st);

	st->ssh = ssh;
	st->client = pcb;
	tcp_arg(pcb, st);
#if 0
	tcp_sent(pcb, ssh_server_sent);
#endif
	tcp_recv(pcb, ssh_server_recv);
	tcp_poll(pcb, ssh_server_poll, SSH_CLIENT_POLL_TIME);
	tcp_err(pcb, ssh_server_err);

	st->cstate = CS_ACCEPT;
	ssh_ringbuffer_flush(&st->rb_tcp_in);
	ssh_ringbuffer_flush(&st->rb_in);
	ssh_ringbuffer_flush(&st->rb_out);

	return ERR_OK;
}


static int user_auth_result_cb(byte res, WS_UserAuthData* auth_data, void* ctx)
{
	ssh_server_t *st = ctx;
	char tmpbuf[64];


	if (st) {
		if (auth_data->type == WOLFSSH_USERAUTH_PUBLICKEY) {
			if (res == WOLFSSH_USERAUTH_SUCCESS) {
				LOG_MSG(LOG_NOTICE,
					"Successful publickey authentication: %s (SHA256:%s)",
					auth_data->username,
					ssh_server_get_pubkey_hash(
						auth_data->sf.publicKey.publicKey,
						auth_data->sf.publicKey.publicKeySz,
						tmpbuf, sizeof(tmpbuf)));
			} else {
				LOG_MSG(LOG_NOTICE, "Failed publickey authentication: %s",
					auth_data->username);
			}
		}
	}

	return WS_SUCCESS;
}


static int receive_cb(WOLFSSH* ssh, void* buf, word32 buf_len, void *ctx)
{
	ssh_server_t *st = ctx;
	size_t size;
	int res;


	if (!ctx)
		return WS_CBIO_ERR_GENERAL;

	if ((size = ssh_ringbuffer_used(&st->rb_tcp_in)) < 1)
		return WS_WANT_READ;

	if (size > buf_len)
		size = buf_len;
	res = ssh_ringbuffer_read(&st->rb_tcp_in, buf, size);

	return (res == 0 ? size : 0);
}


static int send_cb(WOLFSSH* ssh, void* buf, word32 buf_len, void *ctx)
{
	struct ssh_server_t *st = ctx;
	err_t err;


	if (!ctx)
		return WS_CBIO_ERR_GENERAL;

	if (buf_len < 1)
		return 0;

	err = tcp_write(st->client, buf, buf_len, TCP_WRITE_FLAG_COPY);
	if (err != ERR_OK)
		return WS_WANT_WRITE;
	tcp_output(st->client);

	return buf_len;
}


static int user_auth_types_cb(WOLFSSH *ssh, void *ctx)
{
	struct ssh_server_t *st = ctx;
	int ret = 0;

	if (!ssh || !st)
		return WS_BAD_ARGUMENT;

	if (!st->auth_enabled)
		return WOLFSSH_USERAUTH_NONE;
	if (st->auth_cb)
		ret |= WOLFSSH_USERAUTH_PASSWORD | WOLFSSH_USERAUTH_PUBLICKEY;

	return ret;
}


static int user_auth_cb(byte auth_type, WS_UserAuthData* auth_data, void *ctx)
{
        ssh_server_t *st = ctx;
	int ret = WOLFSSH_USERAUTH_FAILURE;


	if (!auth_data || !st)
		return ret;

	if (auth_type == WOLFSSH_USERAUTH_PASSWORD) {
		if (st->auth_cb) {
			if (!st->auth_cb(st->auth_cb_ctx,
						auth_data->username,
						auth_data->usernameSz,
						auth_data->sf.password.password,
						auth_data->sf.password.passwordSz,
						auth_type)) {
				ret = WOLFSSH_USERAUTH_SUCCESS;
				LOG_MSG(LOG_NOTICE, "Successful password authentication: %s",
					auth_data->username);
			} else {
				ret = WOLFSSH_USERAUTH_INVALID_PASSWORD;
				LOG_MSG(LOG_NOTICE, "Failed password authentication: %s",
					auth_data->username);
			}
		}
	}
	else if (auth_type == WOLFSSH_USERAUTH_PUBLICKEY) {
		if (st->auth_cb) {
			if (!st->auth_cb(st->auth_cb_ctx,
						auth_data->username,
						auth_data->usernameSz,
						auth_data->sf.publicKey.publicKey,
						auth_data->sf.publicKey.publicKeySz,
						auth_type)) {
				ret = WOLFSSH_USERAUTH_SUCCESS;
			} else {
				ret = WOLFSSH_USERAUTH_INVALID_PUBLICKEY;
			}
		}
	}
	else if (auth_type == WOLFSSH_USERAUTH_NONE && !st->auth_enabled) {
		ret = WOLFSSH_USERAUTH_SUCCESS;
		LOG_MSG(LOG_NOTICE, "Unauthenticated connection accepted: %s",
			auth_data->username);
	}
	else {
		ret = WOLFSSH_USERAUTH_INVALID_AUTHTYPE;
	}

	return ret;
}


static bool ssh_server_open(ssh_server_t *st) {
	struct tcp_pcb *pcb = NULL;
	WOLFSSH_CTX *ctx = NULL;
	int pkey_count = 0;
	err_t err;

	if (!st)
		return false;

	if (!(ctx = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_SERVER, NULL))) {
		LOG_MSG(LOG_ERR, "ssh_server_open: failed to create SSH context");
		goto panic;
	}

	wolfSSH_CTX_SetBanner(ctx, st->banner);
	wolfSSH_CTX_SetSshProtoIdStr(ctx, "SSH-2.0-picoSSH wolfSSHv"
                                    LIBWOLFSSH_VERSION_STRING "\r\n");
	wolfSSH_SetUserAuth(ctx, user_auth_cb);
	wolfSSH_SetUserAuthResult(ctx, user_auth_result_cb);
	wolfSSH_SetUserAuthTypes(ctx, user_auth_types_cb);
	wolfSSH_SetIORecv(ctx, receive_cb);
	wolfSSH_SetIOSend(ctx, send_cb);

	for (int i = 0; st->pkeys[i].key; i++) {
		const ssh_server_pkey_t *pk = &st->pkeys[i];

		if (wolfSSH_CTX_UsePrivateKey_buffer(ctx, pk->key, pk->key_len, pk->key_type) < 0) {
			LOG_MSG(LOG_ERR, "ssh_server_open: failed to load server private key");
			goto panic;
		}
		pkey_count++;
	}
	if (pkey_count < 1) {
		LOG_MSG(LOG_ERR, "ssh_server_open: no private keys defined");
		goto panic;
	}

	if (!(pcb = tcp_new_ip_type(IPADDR_TYPE_ANY))) {
		LOG_MSG(LOG_ERR, "ssh_server_open: failed to create pcb");
		goto panic;
	}

	if ((err = tcp_bind(pcb, NULL, st->port)) != ERR_OK) {
		LOG_MSG(LOG_ERR, "ssh_server_open: cannot bind to port %d: %d", st->port, err);
		goto panic;
	}

	if (!(st->listen = tcp_listen_with_backlog(pcb, SSH_SERVER_MAX_CONN))) {
		LOG_MSG(LOG_ERR, "ssh_server_open: failed to listen on port %d", st->port);
		goto panic;
	}

	st->ssh_ctx = ctx;
	tcp_arg(st->listen, st);
	tcp_accept(st->listen, ssh_server_accept);

	return true;

panic:
	if (pcb)
		tcp_abort(pcb);
	if (ctx)
		wolfSSH_CTX_free(ctx);

	return false;
}


static err_t ssh_server_close(void *arg) {
	ssh_server_t *st = (ssh_server_t*)arg;
	err_t err = ERR_OK;

	if (!arg)
		return ERR_VAL;

	st->cstate = CS_NONE;

	if (st->client) {
		err = ssh_close_client_connection(st);
	}

	if (st->listen) {
		tcp_arg(st->listen, NULL);
		tcp_accept(st->listen, NULL);
		if ((err = tcp_close(st->listen)) != ERR_OK) {
			LOG_MSG(LOG_NOTICE, "ssh_server_close: failed to close listen pcb: %d",
				err);
			tcp_abort(st->listen);
			err = ERR_ABRT;
		}
		st->listen = NULL;
	}

	if (st->ssh_ctx) {
		wolfSSH_CTX_free(st->ssh_ctx);
		st->ssh_ctx = NULL;
	}

	return err;
}


static void stdio_ssh_out_chars(const char *buf, int length)
{
	size_t len;

	if (!stdio_sshserv)
		return;
	if (stdio_sshserv->cstate != CS_CONNECT)
		return;

	if ((len = ssh_ringbuffer_free(&stdio_sshserv->rb_out)) < 1)
		return;

	if (len > length)
		len = length;

	cyw43_arch_lwip_begin();
	ssh_ringbuffer_add(&stdio_sshserv->rb_out, buf, len, false);
	ssh_server_flush_buffer(stdio_sshserv);
	cyw43_arch_lwip_end();
}


static int stdio_ssh_in_chars(char *buf, int length)
{
	size_t len;

	if (!stdio_sshserv || length < 1)
		return PICO_ERROR_NO_DATA;
	if (stdio_sshserv->cstate != CS_CONNECT)
		return PICO_ERROR_NO_DATA;
	if ((len = ssh_ringbuffer_used(&stdio_sshserv->rb_in)) < 1)
		return PICO_ERROR_NO_DATA;

	if (len > length)
		len = length;

	cyw43_arch_lwip_begin();
	ssh_ringbuffer_read(&stdio_sshserv->rb_in, (uint8_t*)buf, len);
	cyw43_arch_lwip_end();

	return len;
}


static void stdio_ssh_set_chars_available_callback(void (*fn)(void*), void *param)
{
    chars_available_callback = fn;
    chars_available_param = param;
}


static stdio_driver_t stdio_ssh_driver = {
	.out_chars = stdio_ssh_out_chars,
	.in_chars = stdio_ssh_in_chars,
	.set_chars_available_callback = stdio_ssh_set_chars_available_callback,
	.crlf_enabled = PICO_STDIO_DEFAULT_CRLF
};


static void stdio_ssh_init(ssh_server_t *st)
{
	stdio_sshserv = st;
	chars_available_callback = NULL;
	chars_available_param = NULL;
	stdio_set_driver_enabled(&stdio_ssh_driver, true);
}


static void stdio_ssh_close(ssh_server_t *st)
{
	if (!stdio_sshserv || stdio_sshserv != st)
		return;
	stdio_set_driver_enabled(&stdio_ssh_driver, false);
	stdio_sshserv = NULL;
	chars_available_callback = NULL;
	chars_available_param = NULL;
}


bool ssh_server_start(ssh_server_t *st, bool stdio)
{
	cyw43_arch_lwip_begin();
	bool res = ssh_server_open(st);
	if (!res) {
		ssh_server_close(st);
	} else {
		if (stdio)
			stdio_ssh_init(st);
	}
	cyw43_arch_lwip_end();

	return res;
}


void ssh_server_destroy(ssh_server_t *st)
{
	cyw43_arch_lwip_begin();
	stdio_ssh_close(st);
	ssh_server_close(st);
	cyw43_arch_lwip_end();
	ssh_ringbuffer_free(&st->rb_tcp_in);
	ssh_ringbuffer_free(&st->rb_in);
	ssh_ringbuffer_free(&st->rb_out);
}


int ssh_server_add_priv_key(ssh_server_t *st, int type, const uint8_t *key, uint32_t key_len)
{
	if (!st || !key || key_len < 1)
		return -1;

	for (int i = 0; i <= MAX_SERVER_PKEYS; i++) {
		if (st->pkeys[i].key == NULL) {
			st->pkeys[i].key = key;
			st->pkeys[i].key_len = key_len;
			st->pkeys[i].key_type = type;
			return 0;
		}
	}

	return 1;
}


bool ssh_server_client_connected(ssh_server_t *st)
{
	return (st->cstate == CS_NONE ? false : true);
}


err_t ssh_server_get_client_ip(const ssh_server_t *st, ip_addr_t *ip, uint16_t *port)
{
	if (st->cstate == CS_NONE || !st->client)
		return ERR_CONN;

	cyw43_arch_lwip_begin();
	if (ip)
		ip_addr_set(ip, &st->client->remote_ip);
	if (port)
		*port = st->client->remote_port;
	cyw43_arch_lwip_end();

	return ERR_OK;
}


const char* ssh_server_connection_state_name(enum ssh_connection_state state)
{
	switch (state) {
	case CS_NONE:
		return "No Connection";
	case CS_ACCEPT:
		return "Accepted";
	case CS_CONNECT:
		return "Connected";
	}
	return "Unknown";
}


err_t ssh_server_disconnect_client(ssh_server_t *st)
{
	err_t res = ERR_OK;
	ip_addr_t ip;
	uint16_t port;

	if (st->client) {
		cyw43_arch_lwip_begin();
		ip_addr_set(&ip, &st->client->remote_ip);
		port = st->client->remote_port;
		res = ssh_close_client_connection(st);
		cyw43_arch_lwip_end();
		LOG_MSG(LOG_INFO,"SSH Client disconnected: %s:%u", ip4addr_ntoa(&ip), port);
	}
	st->cstate = CS_NONE;

	return res;
}


const char *ssh_server_get_pubkey_hash(const void *pkey, uint32_t pkey_len, char *str_buf, uint32_t str_buf_len)
{
	uint8_t hash[WC_SHA256_DIGEST_SIZE];
	word32 out_len = str_buf_len;

	if (!pkey || pkey_len < 1 || !str_buf || str_buf_len < 1)
		return NULL;

	if (wc_Sha256Hash(pkey, pkey_len, hash))
		return NULL;

	if (Base64_Encode_NoNl(hash, sizeof(hash), (byte*)str_buf, &out_len))
		return NULL;

	if (out_len > str_buf_len)
		out_len = str_buf_len;
	str_buf[out_len - 1] = 0;

	return str_buf;
}


void ssh_server_log_level(int priority)
{
	global_log_level = priority;
}

#if 0
time_t ssh_server_mytime(time_t *t)
{
	time_t tnow = 0;
	struct timespec ts;

	if (aon_timer_is_running()) {
		if (aon_timer_get_time(&ts)) {
			tnow = ts.tv_sec;
		}
	}
	if (t)
		*t = tnow;

	return tnow;
}
#endif

/* eof :-) */
