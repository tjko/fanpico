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
#include <pico_telnetd/sha512crypt.h>

#include "ssh_server.h"

#include "fanpico.h"


static const char *ssh_banner = "\r\n"
	"  _____           ____  _           \r\n"
	" |  ___|_ _ _ __ |  _ \\(_) ___ ___  \r\n"
	" | |_ / _` | '_ \\| |_) | |/ __/ _ \\ \r\n"
	" |  _| (_| | | | |  __/| | (_| (_) |\r\n"
	" |_|  \\__,_|_| |_|_|   |_|\\___\\___/ \r\n"
	" ...ssh...                           \r\n\r\n";


typedef struct user_pwhash_entry {
	const char *login;
	const char *hash;
} user_pwhash_entry_t;


static user_pwhash_entry_t ssh_users[] = {
	{ NULL, NULL },
	{ NULL, NULL }
};

static ssh_server_t *ssh_srv = NULL;


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



static int password_auth_cb(void *ctx, const byte *login, word32 login_len,
			const byte *password, word32 password_len)
{
	user_pwhash_entry_t *users = (user_pwhash_entry_t*)ctx;
	char username[32 + 1];
	char pw[64 + 1];
	int i = 0;
	int res = 2;

	if (login_len >= sizeof(username))
		return -1;
	if (password_len > sizeof(pw))
		return -2;

	memcpy(username, login, login_len);
	username[login_len] = 0;
	memcpy(pw, password, password_len);
	pw[password_len] = 0;

	while (users[i].login) {
		if (!strcmp(username, users[i].login)) {
			/* Found user */
			char *hash = sha512_crypt(pw, users[i].hash);
			if (!strcmp(hash, users[i].hash)) {
				/* password matches */
				log_msg(LOG_NOTICE, "SSH Successful password authentication for: %s",
					username);
				res = 0;
				break;
			} else {
				/* password does not match */
				log_msg(LOG_NOTICE, "SSH Invalid password for: %s", username);
				res = 1;
				break;
			}
		}
		i++;
	}

	if (res == 2) {
		log_msg(LOG_NOTICE, "SSH Unknown user: %s", username);
	}

	memset(pw, 0, sizeof(pw));

	return res;
}



static int pubkey_auth_cb(void *ctx, const byte *login, word32 login_len,
			const byte *pkey, word32 pkey_len)
{
	struct ssh_public_key *pubkeys = (struct ssh_public_key*)ctx;
	char username[32 + 1];
	int res = 2;

	if (login_len >= sizeof(username))
		return -1;

	memcpy(username, login, login_len);
	username[login_len] = 0;

//	printf("pubkey: %s (%u)\n", username, pkey_len);

	for (int i = 0; i < SSH_MAX_PUB_KEYS; i++) {
		struct ssh_public_key *k = &pubkeys[i];

		if (k->pubkey_size == 0)
			continue;

		if (k->pubkey_size != pkey_len)
			continue;

		if (!memcmp(k->pubkey, pkey, pkey_len)) {
			log_msg(LOG_NOTICE, "SSH Successfull public key authentication for: %s (%s)",
				username, k->name);
			res = 0;
			break;
		}
	}

//	printf("pubkey_auth_cb: %d\n", res);
	return res;
}



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

	/* Enable password authentication (only if username configured...) */
	if (strlen(cfg->ssh_user) > 0) {
		ssh_users[0].login = cfg->ssh_user;
		ssh_users[0].hash = cfg->ssh_pwhash;
		ssh_srv->pw_auth_cb = password_auth_cb;
		ssh_srv->pw_auth_cb_ctx = (void*)ssh_users;
	}

	/* Setup SSH public key authentication */
	ssh_srv->pkey_auth_cb = pubkey_auth_cb;
	ssh_srv->pkey_auth_cb_ctx = (void*)cfg->ssh_pub_keys;

	ssh_srv->banner = ssh_banner;
//	ssh_srv->auth_none = true;

	ssh_server_log_level(LOG_INFO);

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


int str_to_ssh_pubkey(const char *s, struct ssh_public_key *pk)
{
	char *t, *str, *saveptr;
	void *buf = NULL;
	int idx = 0;
	int len, key_len;

	if (!s || !pk)
		return -1;
	if (!(str = strdup(s)))
		return -2;

	pk->type[0] = 0;
	pk->name[0] = 0;
	pk->pubkey_size = 0;
	memset(pk->pubkey, 0, sizeof(pk->pubkey));

	t = strtok_r(str, " ", &saveptr);
	while (t && idx < 3) {
		if ((len = strlen(t)) > 0) {
			//printf("%d: '%s'\n", idx, t);
			if (idx == 0) {
				/* key type */
				strncopy(pk->type, t, sizeof(pk->type));
			}
			else if (idx == 1) {
				/* key (base64 encoded) */
				key_len = base64decode_raw(t, len, &buf);
				if (key_len > 0 && key_len <= sizeof(pk->pubkey) &&
					memmem(buf, key_len, pk->type, strlen(pk->type))) {
					memcpy(pk->pubkey, buf, key_len);
					pk->pubkey_size = key_len;
				} else {
					printf("Invalid key!\n");
					break;
				}
			}
			else if (idx == 2) {
				/* key name */
				strncopy(pk->name, t, sizeof(pk->name));
			}
			idx++;
		}
		t = strtok_r(NULL, " ", &saveptr);
	}

	free(str);
	if (buf)
		free(buf);

	return (idx >= 2 ? 0 : 1);
}


const char* ssh_pubkey_to_str(const struct ssh_public_key *pk, char *s, size_t s_len)
{
	char *e;

	if (!pk || !s || s_len < 1)
		return NULL;

	if (!(e = base64encode_raw(pk->pubkey, pk->pubkey_size)))
		return NULL;

	snprintf(s, s_len, "%s %s %s", pk->type, e, pk->name);
	s[s_len - 1] = 0;
	free(e);

	return s;
}


time_t ssh_server_myTime(time_t *t)
{
	printf("myTime(%p)\n", t);
	rtc_get_time(t);
	printf("myTime: %llu\n", *t);
	return *t;
}


