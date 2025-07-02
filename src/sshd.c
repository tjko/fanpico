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
#ifdef WOLFCRYPT_TEST
#include <wolfcrypt/test/test.h>
#endif

#include "ssh/ssh_server.h"
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



void sshserver_init()
{
	char *buf = NULL;
	uint32_t buf_size = 0;
	int pkey_count = 0;

	wolfSSL_Init();
//	wolfSSL_Debugging_ON();
#ifdef WOLFCRYPT_TEST
	printf("wolfcrypt_test(): %d\n", wolfcrypt_test(NULL));
#endif

	if (wolfSSH_Init() != WS_SUCCESS) {
		log_msg(LOG_ERR, "Failed to initialize wolfSSH library.");
		return;
	}
//	wolfSSH_Debugging_ON();

	/* Create SSH server instance */
	if (!(ssh_srv = ssh_server_new(2048, 8192))) {
		log_msg(LOG_ERR, "Failed to initialize SSH server.");
		return;
	}

	/* Load SSH server private keys */
	for (int i = 0; pkey_algorithms[i].name; i++) {
		const ssh_pkey_alg_t *alg = &pkey_algorithms[i];

		if (flash_read_file(&buf, &buf_size, alg->filename) == 0) {
			log_msg(LOG_DEBUG, "Found SSH private key: %s (%lu)",
				alg->filename, buf_size);
			if (ssh_server_add_priv_key(ssh_srv, WOLFSSH_FORMAT_ASN1,
							(uint8_t*)buf, buf_size)) {
				log_msg(LOG_NOTICE, "Failed to add private key.\n");
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

	ssh_srv->auth_enabled = cfg->ssh_auth;
	ssh_srv->auth_cb_ctx = (void*)ssh_users;
	ssh_srv->banner = ssh_banner;
	ssh_srv->log_cb = log_msg;

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
	char *buf = NULL;
	char tmp[64];
	uint32_t buf_size = 0;
	int i = 0;

	while(pkey_algorithms[i].name) {
		const ssh_pkey_alg_t *alg = &pkey_algorithms[i++];
		int res = flash_read_file(&buf, &buf_size, alg->filename);

		printf("%-20s ", alg->name);
		if (res == 0) {
			printf("%8lu SHA256:%s\n", buf_size,
				ssh_server_get_pubkey_hash(buf, buf_size, tmp, sizeof(tmp)));
			free(buf);
		} else {
			printf("<No Key>\n");
		}
	}
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


/* eof :-) */
