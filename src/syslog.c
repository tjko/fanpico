/* syslog.c
   Copyright (C) 2022 Timo Kokkonen <tjko@iki.fi>

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
#include <stdarg.h>
#include <time.h>
#include <assert.h>
#include "hardware/rtc.h"
#include "pico/stdlib.h"
#include "pico/util/datetime.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include "fanpico.h"
#include "syslog.h"


typedef struct syslog_t_ {
	ip_addr_t server_addr;
	u16_t port;
	int facility;
	char *hostname;
	struct udp_pcb *pcb;
} syslog_t;

static syslog_t *syslog = NULL;


syslog_t* syslog_init(const ip_addr_t *ipaddr, u16_t port)
{
	syslog_t *ctx = calloc(1, sizeof(syslog_t));
	if (!ctx) {
		printf("syslog_init(): not enough memory!\n");
		return NULL;
	}

	cyw43_arch_lwip_begin();

	ctx->pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
	if (!ctx->pcb) {
		printf("syslog_init(): failed to create pcb\n");
		goto error;
	}
	ctx->server_addr = *ipaddr;
	ctx->port = (port > 0 ? port : SYSLOG_DEFAULT_PORT);

	if (udp_bind(ctx->pcb, IP_ANY_TYPE, 514) != ERR_OK) {
		printf("syslog_init(): udb bind failed\n");
		goto error;
	}

	if (udp_connect(ctx->pcb, &ctx->server_addr, ctx->port) != ERR_OK) {
		printf("syslog_init(): udp connect failed\n");
		goto error;
	}

	cyw43_arch_lwip_end();
	return ctx;


error:
	if (ctx) {
		if (ctx->pcb)
			udp_remove(ctx->pcb);
		free(ctx);
	}
	cyw43_arch_lwip_end();
	return NULL;
}


int syslog_open(const ip_addr_t *server, u16_t port, int facility, const char *hostname)
{
	if (!server || !hostname)
		return 0;

	syslog = syslog_init(server, port);
	if (syslog) {
		syslog->facility = facility;
		syslog->hostname = strdup(hostname);
	}
	return (syslog ? 0 : 1);
}


void syslog_close()
{
	if (!syslog)
		return;

	if (syslog->hostname)
		free(syslog->hostname);

	cyw43_arch_lwip_begin();
	if (syslog->pcb) {
		udp_disconnect(syslog->pcb);
		udp_remove(syslog->pcb);
	};
	free(syslog);
	syslog = NULL;
	cyw43_arch_lwip_end();
}


int syslog_msg(int severity, const char *format, ...)
{
	va_list args;

	char msg[SYSLOG_MAX_MSG_LEN - 64];
	char buf[SYSLOG_MAX_MSG_LEN];
	char tstamp[16];
	datetime_t t;
	struct tm tm;
	uint16_t msg_len;
	int pri;


	if (!format || !syslog)
		return 1;
	if (!rtc_get_datetime(&t))
		return 2;

	/* compute syslog priority */
	pri = (syslog->facility << 3) | (severity & 0x07);
	/* generate syslog timestamp */
	datetime_to_tm(&t, &tm);
	strftime(tstamp, sizeof(tstamp), "%b %e %T", &tm);
	/* format the message */
	va_start(args, format);
	vsnprintf(msg, sizeof(msg), format, args);
	va_end(args);
	/* build the syslog 'packet' */
	snprintf(buf, sizeof(buf), "<%d>%s %s %s", pri, tstamp, syslog->hostname, msg);
	msg_len = strlen(buf);
	debug(3, "SYSLOG: '%s'\n", buf);

	cyw43_arch_lwip_begin();
	struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, msg_len, PBUF_RAM);
	if (p) {
		memcpy(p->payload, buf, msg_len);
		udp_send(syslog->pcb, p);
		pbuf_free(p);
	}
	cyw43_arch_lwip_end();

	return 0;
}
