/* ssh_util.c
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
#include <wolfssl/wolfcrypt/wc_port.h>
#include <wolfssl/wolfcrypt/asn.h>
#include <wolfssh/keygen.h>
#include <pico-sshd.h>

#include "fanpico.h"


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
	return wolfSSH_MakeEd25519Key(buf, buf_size, WOLFSSH_ED25519KEY);
}


static const ssh_pkey_alg_t pkey_algorithms[] = {
	{ "ecdsa", "ssh-ecdsa.der", create_ecdsa_key },
	{ "ed25519", "ssh-ed25519.der", create_ed25519_key },
#ifndef NO_RSA
	{ "rsa", "ssh-rsa.der", create_rsa_key },
#endif
	{ NULL, NULL, NULL }
};


void ssh_list_pkeys()
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


int ssh_create_pkey(const char* args)
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


int ssh_delete_pkey(const char* args)
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


int ssh_get_pkey(int index, char** buf_ptr, uint32_t* buf_size_ptr, const char** name_ptr)
{
	if (index < 0 || !buf_ptr || !buf_size_ptr || !name_ptr)
		return -1;

	*name_ptr = "";

	/* Find the key... */
	for (int i = 0; pkey_algorithms[i].name; i++) {
		const ssh_pkey_alg_t *alg = &pkey_algorithms[i];

		if (i != index)
			continue;

		if (flash_read_file(buf_ptr, buf_size_ptr, alg->filename)) {
			/* No key found... */
			return 0;
		}

		/* Found the key... */
		log_msg(LOG_DEBUG, "Found SSH private key: %s (%lu)",
			alg->filename, *buf_size_ptr);
		*name_ptr = alg->name;
		return 1;
	}

	return -2;
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


/* eof :-) */
