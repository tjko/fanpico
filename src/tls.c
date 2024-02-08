/* tls.c
   Copyright (C) 2021-2022 Timo Kokkonen <tjko@iki.fi>

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
#include <malloc.h>
#include "pico/stdlib.h"
#include "fanpico.h"

#if WIFI_SUPPORT
#if TLS_SUPPORT
#include "mbedtls/error.h"
#include "lwip/altcp_tls.h"
#endif


int read_pem_file(char *buf, uint32_t size, uint32_t timeout, bool append)
{
	absolute_time_t t_timeout = get_absolute_time();
	char tmp[128], *line;
	int state = 0;
	int r;

	if (!buf || size < 2)
		return -1;

	tmp[0] = 0;
	if (!append)
		buf[0] = 0;

	while (1) {
		r = getstring_timeout_ms(tmp, sizeof(tmp), 100);
		if (r < 0)
			break;
		if (r > 0) {
			line = trim_str(tmp);
			if (state == 0) {
				if (!strncmp(line, "-----BEGIN", 10)) {
					state = 1;
				} else {
					break;
				}
			}
			else if (state == 1) {
				if (!strncmp(line, "-----END", 8)) {
					state = 2;
				}
			}

			if (state > 0) {
				strncatenate(buf, line, size);
				strncatenate(buf, "\n", size);
			}
			if (state == 2)
				return 1;
			tmp[0] = 0;
		}
		if (time_passed(&t_timeout, timeout)) {
			break;
		}
	}

	return 0;
}

#if TLS_SUPPORT
struct altcp_tls_config* tls_server_config()
{
	struct altcp_tls_config *c = NULL;
	char *key = NULL, *cert = NULL;
	uint32_t key_size, cert_size;
	int res;

	log_msg(LOG_DEBUG, "tls_server_config(): started");
	res = flash_read_file(&cert, &cert_size, "cert.pem");
	if (res == 0) {
		//printf("cert:\n%s\n", cert);
		res = flash_read_file(&key, &key_size, "key.pem");
		if (res == 0) {
			//printf("key:\n%s\n", key);
			c = altcp_tls_create_config_server_privkey_cert((const u8_t*)key, key_size,
									NULL, 0,
									(const u8_t*)cert, cert_size);
		}
	}

	if (key)
		free(key);
	if (cert)
		free(cert);

	log_msg(LOG_DEBUG, "tls_server_config(): finished (%lx)", (uint32_t)c);
	return c;
}
#endif

#endif /* WIFI_SUPPORT */
