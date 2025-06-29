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
#include <time.h>
#include <assert.h>
#include "pico/stdlib.h"
#include "pico/stdio/driver.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "wolfssl/wolfcrypt/settings.h"
#include <wolfssl/wolfcrypt/ed25519.h>
#include <wolfssl/ssl.h>
#include <wolfssh/ssh.h>
#include <wolfssh/keygen.h>
#include <wolfcrypt/test/test.h>

#include "fanpico.h"


static const char *ssh_banner = "\r\n"
	"  _____           ____  _           \r\n"
	" |  ___|_ _ _ __ |  _ \\(_) ___ ___  \r\n"
	" | |_ / _` | '_ \\| |_) | |/ __/ _ \\ \r\n"
	" |  _| (_| | | | |  __/| | (_| (_) |\r\n"
	" |_|  \\__,_|_| |_|_|   |_|\\___\\___/ \r\n"
	" ...ssh...                           \r\n\r\n";


static const char *ssh_default_banner = "\r\npico-sshd\r\n\r\n";


#define SSH_DEFAULT_PORT 22
#define SSH_SERVER_MAX_CONN 1
#define SSH_CLIENT_POLL_TIME 1

#define MAX_LOGIN_LENGTH 32
#define MAX_PASSWORD_LENGTH 64
#define MAX_LOGIN_FAILURES 3
#define MAX_SERVER_PKEYS 3

typedef struct user_pwhash_entry {
	const char *login;
	const char *hash;
} user_pwhash_entry_t;

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
	uint8_t login[32];

	/* Configuration options... set before calling telnet_server_start() */
	uint16_t port;             /* Listen port (default tcp/22) */
	const char *banner;        /* Login banner string to display when connection starts. */
	ssh_server_pkey_t pkeys[MAX_SERVER_PKEYS];  /* server private key(s) */
	void (*log_cb)(int priority, const char *format, ...);
	int (*auth_cb)(void* param, const char *login, const char *password);
	void *auth_cb_param;
} ssh_server_t;


static user_pwhash_entry_t ssh_users[] = {
	{ NULL, NULL },
	{ NULL, NULL }
};


static ssh_server_t *stdio_sshserv = NULL;
static void (*chars_available_callback)(void*) = NULL;
static void *chars_available_param = NULL;


// --ssh-keygen--

typedef struct ssh_pkey_alg_t {
	char *name;
	char *filename;
	int (*create_key)(void *buf, size_t buf_size);
} ssh_pkey_alg_t;


#ifndef NO_RSA
static int create_rsa_key(void *buf, size_t buf_size)
{
	return wolfSSH_MakeRsaKey(buf, buf_size, WOLFSSH_RSAKEY_DEFAULT_SZ,
		WOLFSSH_RSAKEY_DEFAULT_E);
}
#endif

static int create_ecdsa_key(void *buf, size_t buf_size)
{
	return wolfSSH_MakeEcdsaKey(buf, buf_size, WOLFSSH_ECDSAKEY_PRIME256);
}

static int create_ed25519_key(void *buf, size_t buf_size)
{
    WC_RNG rng;
    ed25519_key key;
    int ret = WS_SUCCESS;
    int key_size;

    if (wc_InitRng(&rng))
	return WS_CRYPTO_FAILED;

    if (wc_ed25519_init(&key))
            ret = WS_CRYPTO_FAILED;

    if (ret == WS_SUCCESS) {
            ret = wc_ed25519_make_key(&rng, 256/8, &key);
            if (ret)
		    ret = WS_CRYPTO_FAILED;
            else
		    ret = WS_SUCCESS;
    }

    if (ret == WS_SUCCESS) {
            if ((key_size = wc_Ed25519KeyToDer(&key, buf, buf_size)) < 0)
                    ret = WS_CRYPTO_FAILED;
            else
		    ret = key_size;
        }

    wc_ed25519_free(&key);

    if (wc_FreeRng(&rng))
            ret = WS_CRYPTO_FAILED;

    return ret;
}


static const ssh_pkey_alg_t pkey_algorithms[] = {
	{ "ecdsa", "ssh-ecdsa.key", create_ecdsa_key },
	{ "ed25519", "ssh-ed25519.key", create_ed25519_key },
#ifndef NO_RSA
	{ "rsa", "ssh-rsa.key", create_rsa_key },
#endif
	{ NULL, NULL, NULL }
};

// --ringbuffer--

int ssh_ringbuffer_init(ssh_ringbuffer_t *rb, void *buf, size_t size)
{
	if (!rb)
		return -1;

	if (!buf) {
		if (!(rb->buf = calloc(1, size)))
			return -2;
		rb->free_buf = true;
	} else {
		rb->buf = buf;
		rb->free_buf = false;
	}

	rb->size = size;
	rb->free = size;
	rb->head = 0;
	rb->tail = 0;

	return 0;
}


int ssh_ringbuffer_destroy(ssh_ringbuffer_t *rb)
{
	if (!rb)
		return -1;

	if (rb->free_buf && rb->buf)
		free(rb->buf);

	rb->buf = NULL;
	rb->size = 0;
	rb->free = 0;
	rb->head = 0;
	rb->tail = 0;

	return 0;
}


int ssh_ringbuffer_flush(ssh_ringbuffer_t *rb)
{
	if (!rb)
		return -1;

	rb->head = 0;
	rb->tail = 0;
	rb->free = rb->size;

	return 0;
}

inline size_t ssh_ringbuffer_used(ssh_ringbuffer_t *rb)
{
	return (rb ? rb->size - rb->free : 0);
}

inline size_t ssh_ringbuffer_free(ssh_ringbuffer_t *rb)
{
	return (rb ? rb->free : 0);
}


inline size_t ssh_ringbuffer_offset(ssh_ringbuffer_t *rb, size_t offset, size_t delta, int direction)
{
	size_t o = offset % rb->size;
	size_t d = delta % rb->size;

	if (!rb)
		return 0;
	if (d == 0)
		return o;

	if (direction >= 0) {
		o = (o + d) % rb->size;
	} else {
		if (o < d) {
			o = rb->size - (d - o);
		} else {
			o -= d;
		}
	}

	return o;
}


int ssh_ringbuffer_add(ssh_ringbuffer_t *rb, const void *data, size_t len, bool overwrite)
{
	if (!rb || !data)
		return -1;

	if (len == 0)
		return 0;

	if (len > rb->size)
		return -2;

	if (overwrite && rb->free < len) {
		size_t needed = len - rb->free;
		rb->head = ssh_ringbuffer_offset(rb, rb->head, needed, 1);
		rb->free += needed;
	}
	if (rb->free < len)
		return -3;

	size_t new_tail = ssh_ringbuffer_offset(rb, rb->tail, len, 1);

	if (new_tail > rb->tail) {
		memcpy(rb->buf + rb->tail, data, len);
	} else {
		size_t part1 = rb->size - rb->tail;
		size_t part2 = len - part1;
		memcpy(rb->buf + rb->tail, data, part1);
		memcpy(rb->buf, data + part1, part2);
	}

	rb->tail = new_tail;
	rb->free -= len;

	return 0;
}


int ssh_ringbuffer_read(ssh_ringbuffer_t *rb, uint8_t *ptr, size_t size)
{
	if (!rb || size < 1)
		return -1;

	size_t used = rb->size - rb->free;
	if (used < size)
		return -2;

	size_t new_head = ssh_ringbuffer_offset(rb, rb->head, size, 1);

	if (ptr) {
		if (new_head > rb->head) {
			memcpy(ptr, rb->buf + rb->head, size);
		} else {
			size_t part1 = rb->size - rb->head;
			size_t part2 = size - part1;
			memcpy(ptr, rb->buf + rb->head, part1);
			memcpy(ptr + part1, rb->buf, part2);
		}
	}

	rb->head = new_head;
	rb->free += size;

	return 0;
}

inline int ssh_ringbuffer_read_char(ssh_ringbuffer_t *rb)
{
	if (!rb)
		return -1;
	if (rb->head == rb->tail)
		return -2;

	int val = rb->buf[rb->head];
	rb->head = ssh_ringbuffer_offset(rb, rb->head, 1, 1);
	rb->free++;

	return val;
}

inline int ssh_ringbuffer_add_char(ssh_ringbuffer_t *rb, uint8_t ch, bool overwrite)
{
	if (!rb)
		return -1;

	if (overwrite && rb->free < 1) {
		rb->head = ssh_ringbuffer_offset(rb, rb->head, 1, 1);
		rb->free += 1;
	}
	if (rb->free < 1)
		return -2;

	rb->buf[rb->tail] = ch;
	rb->tail = ssh_ringbuffer_offset(rb, rb->tail, 1, 1);
	rb->free--;

	return 0;
}


size_t ssh_ringbuffer_peek(ssh_ringbuffer_t *rb, void **ptr, size_t size)
{
	if (!rb || !ptr || size < 1)
		return 0;

	*ptr = NULL;
	size_t used = rb->size - rb->free;
	size_t toread = (size < used ? size : used);
	size_t len = rb->size - rb->head;

	if (used < 1)
		return 0;

	*ptr = rb->buf + rb->head;

	return (len < toread ? len : toread);
}


// ---log---

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

#if 0
struct log_priority {
	uint8_t  priority;
	const char *name;
};

static const struct log_priority log_priorities[] = {
	{ LOG_EMERG,   "EMERG" },
	{ LOG_ALERT,   "ALERT" },
	{ LOG_CRIT,    "CRIT" },
	{ LOG_ERR,     "ERR" },
	{ LOG_WARNING, "WARNING" },
	{ LOG_NOTICE,  "NOTICE" },
	{ LOG_INFO,    "INFO" },
	{ LOG_DEBUG,   "DEBUG" },
	{ 0, NULL }
};

static const char* log_priority2str(int pri)
{
	int i = 0;

	while(log_priorities[i].name) {
		if (log_priorities[i].priority == pri)
			return log_priorities[i].name;
		i++;
	}

	return NULL;
}
#endif

void sshd_log_level(int priority)
{
	global_log_level = priority;
}




void sshd_log_msg(int priority, const char *format, ...)
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
	printf("%s %s %s\n", tstamp, log_priority2str(priority), buf);

	free(buf);
}



#ifndef LOG_MSG
#define LOG_MSG(...) { if (st) if (st->log_cb) st->log_cb(__VA_ARGS__); }
#endif


static ssh_server_t* ssh_server_init(size_t rxbuf_size, size_t txbuf_size)
{
	ssh_server_t *st = calloc(1, sizeof(ssh_server_t));

	if (!st)
		return NULL;

	st->ssh_ctx = NULL;
	st->ssh = NULL;
	st->cstate = CS_NONE;
	st->log_cb = sshd_log_msg;
	st->auth_cb = NULL;
	st->port = SSH_DEFAULT_PORT;
	st->banner = ssh_default_banner;

	st->login[0] = 0;

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
//	printf("wolfSSH_worker: res=%d (channel=%u)\n", res, channel_id);

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


//	LOG_MSG(LOG_DEBUG, "ssh_server_recv: data received (pcb=%x): tot_len=%d, len=%d, err=%d",
//		pcb, p->tot_len, p->len, err);

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

//	printf("ssh_server_flush_buffer(%p): start (%d)\n", st, st->cstate);

	if (st->cstate != CS_CONNECT)
		return 0;

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


static const char* connected_text = "Connected.\r\n";

static err_t ssh_server_poll(void *arg, struct tcp_pcb *pcb)
{
	ssh_server_t *st = (ssh_server_t*)arg;
	int res, err;

	if (st->cstate == CS_ACCEPT) {
//		LOG_MSG(LOG_INFO, "calling wolfSSH_accept(%p)", st->ssh);
		if ((res = wolfSSH_accept(st->ssh)) == WS_SUCCESS) {
			LOG_MSG(LOG_INFO, "SSH accept ok");
			st->cstate = CS_CONNECT;
			ssh_ringbuffer_add(&st->rb_out, connected_text, strlen(connected_text), true);
		} else {
			err = wolfSSH_get_error(st->ssh);
			if (err == WS_WANT_READ || err == WS_WANT_WRITE)
				return ERR_OK;
			LOG_MSG(LOG_INFO, "wolfSSH_accept(): failed: %d (%d)", err, res);
			//wolfSSH_stream_exit(st->ssh, 0);
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
	wolfSSH_SetIOReadCtx(ssh, st);
	wolfSSH_SetIOWriteCtx(ssh, st);

	st->ssh = ssh;
	st->client = pcb;
	tcp_arg(pcb, st);
//	tcp_sent(pcb, ssh_server_sent);
	tcp_recv(pcb, ssh_server_recv);
	tcp_poll(pcb, ssh_server_poll, SSH_CLIENT_POLL_TIME);
	tcp_err(pcb, ssh_server_err);

	st->cstate = CS_ACCEPT;
	ssh_ringbuffer_flush(&st->rb_tcp_in);
	ssh_ringbuffer_flush(&st->rb_in);
	ssh_ringbuffer_flush(&st->rb_out);

	return ERR_OK;
}



static int user_auth_result_cb(byte res, WS_UserAuthData* authData, void* ctx)
{
	printf("user_auth_result_cb(): %s\n",
		(res == WOLFSSH_USERAUTH_SUCCESS ? "OK" : "FAIL"));
	(void)authData;
	(void)ctx;
	return WS_SUCCESS;
}


static int user_auth_cb(byte auth_type, WS_UserAuthData* auth_data, void *ctx)
{
        ssh_server_t *st = ctx;

	printf("user_auth_cb(%d,%p,%p)\n", auth_type, auth_data, ctx);

	if (!st)
		return WOLFSSH_USERAUTH_FAILURE;


	if (auth_type == WOLFSSH_USERAUTH_PASSWORD) {
		return WOLFSSH_USERAUTH_INVALID_PASSWORD;
	}
	else if (auth_type == WOLFSSH_USERAUTH_PUBLICKEY) {
		return WOLFSSH_USERAUTH_SUCCESS;
	}

	return WOLFSSH_USERAUTH_FAILURE;
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


const char* ssh_connection_state_name(enum ssh_connection_state state)
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


static err_t ssh_server_disconnect_client(ssh_server_t *st)
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
		LOG_MSG(LOG_INFO,"Client disconnected: %s:%u", ip4addr_ntoa(&ip), port);
	}
	st->cstate = CS_NONE;

	return res;
}


static ssh_server_t *ssh_srv = NULL;

void sshserver_init()
{
	int res;
	char *buf = NULL;
	uint32_t buf_size = 0;
	int pkey_count = 0;

	wolfSSL_Init();
//	wolfSSL_Debugging_ON();
#if 0
	res = wolfcrypt_test(NULL);
	printf("End: %d\n", res);
#endif

	if (wolfSSH_Init() != WS_SUCCESS) {
		log_msg(LOG_ERR, "Failed to initialize wolfSSH library.");
		return;
	}
//	wolfSSH_Debugging_ON();

	ssh_srv = ssh_server_init(2048, 8192);
	if (!ssh_srv) {
		log_msg(LOG_ERR, "Failed to initialize SSH server.");
		return;
	}

	/* Load server private keys */
	for (int i = 0; pkey_algorithms[i].name; i++) {
		const ssh_pkey_alg_t *alg = &pkey_algorithms[i];

		res = flash_read_file(&buf, &buf_size, alg->filename);
		if (res == 0 && pkey_count < MAX_SERVER_PKEYS) {
			log_msg(LOG_DEBUG, "Found SSH private key: %s (%lu)",
				alg->filename, buf_size);
			ssh_srv->pkeys[pkey_count].key = (uint8_t*)buf;
			ssh_srv->pkeys[pkey_count].key_len = buf_size;
			ssh_srv->pkeys[pkey_count].key_type = WOLFSSH_FORMAT_ASN1;
			pkey_count++;
		}
	}

 	ssh_users[0].login = cfg->ssh_user;
	ssh_users[0].hash = cfg->ssh_pwhash;

	ssh_srv->banner = ssh_banner;

	sshd_log_level(LOG_INFO);

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
			ssh_connection_state_name(ssh_srv->cstate));
	} else {
		printf("No active SSH connection(s)\n");
	}
}


void sshserver_list_pkeys()
{
	printf("list pkeys\n");

}

int sshserver_create_pkey(const char* args)
{
	const size_t buf_size = 4096;
	int kcount = 0;
	bool create_all = false;
	byte *buf;
	int res;


	if (!strncasecmp(args, "all", 4))
		create_all = true;

	for (int i = 0; pkey_algorithms[i].name; i++) {
		const ssh_pkey_alg_t *alg = &pkey_algorithms[i];

		if (!create_all && strncasecmp(args, alg->name, strlen(alg->name) + 1))
			continue;

		if (flash_file_size(alg->filename) > 0) {
			printf("%s key already exists!\n", alg->name);
			continue;
		}

		printf("Generating %s private key...", alg->name);
		if (!(buf = calloc(1, buf_size))) {
			printf("Out or memory!\n");
				return 2;
		}
		res = alg->create_key(buf, buf_size);
		if (res > 0) {
			res = flash_write_file((const char*)buf, res, alg->filename);
			if (res) {
				printf("Failed to save private key! (%d)\n", res);
			} else {
				printf("OK\n");
				kcount++;
			}
		} else {
			printf("Failed to generate key! (%d)\n", res);
		}
		free(buf);
	}

	return (kcount > 0 ? 0 : 1);
}

int sshserver_delete_pkey(const char* args)
{
	int res;

	printf("delete pkey: %s\n", args);

	for (int i = 0; pkey_algorithms[i].name; i++) {
		const ssh_pkey_alg_t *alg = &pkey_algorithms[i];

		if (!strncasecmp(args, alg->name, strlen(alg->name) + 1)) {
			printf("Deleting %s private key...\n", alg->name);
			res = flash_delete_file(alg->filename);
			if (res == -2) {
				printf("No private key present.\n");
				res = 0;
			}
			else if (res) {
				printf("Failed to delete private key: %d\n", res);
				res = 2;
			} else {
				printf("Private key deleted.\n");
			}

			return res;
		}
	}

	return 1;
}


time_t ssh_server_myTime(time_t *t)
{
	printf("myTime(%p)\n", t);
	rtc_get_time(t);
	printf("myTime: %llu\n", *t);
	return *t;
}


