/* dhcp.c
   Copyright (C) 2022-2025 Timo Kokkonen <tjko@iki.fi>

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
#include <time.h>
#include <assert.h>
#include "pico/cyw43_arch.h"
#include "lwip/prot/dhcp.h"

#include "fanpico.h"



#define DHCP_OPTION_LOG        7
#define DHCP_OPTION_POSIX_TZ   100


/* LwIP DHCP hook to customize option 55 (Parameter-Request) when DHCP
 * client send DHCP_REQUEST message...
 */
void pico_dhcp_option_add_hook(struct netif *netif, struct dhcp *dhcp, u8_t state, struct dhcp_msg *msg,
			u8_t msg_type, u16_t *options_len_ptr)
{
	u8_t new_parameters[] = {
		DHCP_OPTION_LOG,
		DHCP_OPTION_POSIX_TZ,
		0
	};
	u16_t extra_len = sizeof(new_parameters) - 1;
	u16_t orig_len = *options_len_ptr;
	u16_t old_ptr = 0;
	u16_t new_ptr = 0;
	struct dhcp_msg tmp;

	if (msg_type != DHCP_REQUEST)
		return;

	LWIP_ASSERT("dhcp option overflow", *options_len_ptr + extra_len <= DHCP_OPTIONS_LEN);

	/* Copy options to temporary buffer, so we can 'edit' option 55... */
	memcpy(tmp.options, msg->options, sizeof(tmp.options));

	/* Rebuild options... */
	while (old_ptr < orig_len) {
		u8_t code = tmp.options[old_ptr++];
		u8_t len = tmp.options[old_ptr++];

		msg->options[new_ptr++] = code;
		msg->options[new_ptr++] = (code == 55 ? len + extra_len : len);
		for (int i = 0; i < len; i++) {
			msg->options[new_ptr++] = tmp.options[old_ptr++];
		}
		if (code == 55) {
			for (int i = 0; i < extra_len; i++) {
				msg->options[new_ptr++] = new_parameters[i];
			}
		}
	}
	*options_len_ptr = new_ptr;
}


/* LwIP DHCP hook to parse additional DHCP options received.
 */
void pico_dhcp_option_parse_hook(struct netif *netif, struct dhcp *dhcp, u8_t state, struct dhcp_msg *msg,
             u8_t msg_type, u8_t option, u8_t option_len, struct pbuf *pbuf, u16_t option_value_offset)
{
	struct persistent_memory_block *m = persistent_mem;
	ip4_addr_t log_ip;
	char timezone[64];

	if (msg_type != DHCP_ACK)
		return;

	log_msg(LOG_DEBUG, "Parse DHCP option (msg=%02x): %u (len=%u,offset=%u)",
		msg_type, option, option_len, option_value_offset);

	if (option == DHCP_OPTION_LOG && option_len >= 4) {
		memcpy(&log_ip.addr, pbuf->payload + option_value_offset, 4);
		if (ip_addr_isany(&cfg->syslog_server)) {
			/* If no syslog server configured, use one from DHCP... */
			ip_addr_copy(net_state->syslog_server, log_ip);
			mutex_exit(config_mutex);
			log_msg(LOG_INFO, "Using Log Server from DHCP: %s", ip4addr_ntoa(&log_ip));
		} else {
			log_msg(LOG_DEBUG, "Ignoring Log Server from DHCP: %s",
				ip4addr_ntoa(&log_ip));
		}
	}
	else if (option == DHCP_OPTION_POSIX_TZ && option_len > 0) {
		int  len = (option_len < sizeof(timezone) ? option_len : sizeof(timezone) - 1);
		memcpy(timezone, pbuf->payload + option_value_offset, len);
		timezone[len] = 0;
		if (strlen(cfg->timezone) < 1) {
			log_msg(LOG_INFO, "Using timezone from DHCP: %s", timezone);
			if (strncmp(timezone, m->timezone, len + 1)) {
				update_persistent_memory_tz(timezone);
				setenv("TZ", m->timezone, 1);
				tzset();
			}
		} else {
			log_msg(LOG_DEBUG, "Ignoring timezone from DHCP: %s", timezone);
		}
	}
}

