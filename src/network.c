/* network.c
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
#include <time.h>
#include <assert.h>
#include "hardware/rtc.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "pico/util/datetime.h"
#ifdef LIB_PICO_CYW43_ARCH
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/prot/dhcp.h"
#include "lwip/apps/sntp.h"
#include "lwip/apps/httpd.h"
#endif

#include "fanpico.h"


#ifdef WIFI_SUPPORT

#include "syslog.h"

u16_t fanpico_ssi_handler(const char *tag, char *insert, int insertlen,
			u16_t current_tag_part, u16_t *next_tag_part);


static bool wifi_initialized = false;
static bool network_initialized = false;
static uint8_t cyw43_mac[6];
static char wifi_hostname[32];
static ip_addr_t syslog_server;

void wifi_mac()
{
	printf("%s\n", mac_address_str(cyw43_mac));
}

void wifi_link_cb(struct netif *netif)
{
	log_msg(LOG_WARNING, "WiFi Link: %s", (netif_is_link_up(netif) ? "UP" : "DOWN"));
}

void wifi_status_cb(struct netif *netif)
{
	log_msg(LOG_WARNING, "WiFi Status: %s", (netif_is_up(netif) ? "UP" : "DOWN"));

	if (netif_is_up(netif) && !network_initialized) {
		syslog_open(&syslog_server, 0, LOG_USER, wifi_hostname);
		log_msg(LOG_DEBUG, "Network initialization complete.");
		network_initialized = true;
	}
}

void wifi_init()
{
	uint32_t country_code = CYW43_COUNTRY_WORLDWIDE;
	struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
	pico_unique_board_id_t b;
	int res;

	memset(cyw43_mac, 0, sizeof(cyw43_mac));
	ip_addr_set_zero(&syslog_server);

	/* If WiFi country is defined in configuratio, use it... */
	if (cfg) {
		char *country = cfg->wifi_country;
		if (strlen(country) > 1) {
			uint rev = 0;
			if (country[2] >= '0' && country[2] <= '9') {
				rev = country[2] - '0';
			}
			country_code = CYW43_COUNTRY(country[0], country[1], rev);
			log_msg(LOG_DEBUG, "WiFi country code: %06x", country_code);
		}
	}

	if ((res = cyw43_arch_init_with_country(country_code))) {
		log_msg(LOG_ALERT, "WiFi initialization failed: %d", res);
		return;
	}

	cyw43_arch_enable_sta_mode();

	/* Set WiFi interface hostname... */
	pico_get_unique_board_id(&b);
	snprintf(wifi_hostname, sizeof(wifi_hostname),
		"FanPico-%02x%02x%02x%02x%02x%02x%02x%02x",
		b.id[0],b.id[1],b.id[2],b.id[3],
		b.id[4],b.id[5],b.id[6],b.id[7]);
	log_msg(LOG_INFO, "WiFi hostname: %s", wifi_hostname);
	netif_set_hostname(n, wifi_hostname);

	netif_set_link_callback(n, wifi_link_cb);
	netif_set_status_callback(n, wifi_status_cb);
	if (!ip_addr_isany(&cfg->ip)) {
		dhcp_stop(n);
		log_msg(LOG_INFO, "     IP: %s", ipaddr_ntoa(&cfg->ip));
		log_msg(LOG_INFO, "Netmask: %s", ipaddr_ntoa(&cfg->netmask));
		log_msg(LOG_INFO, "Gateway: %s", ipaddr_ntoa(&cfg->gateway));
		netif_set_addr(n, &cfg->ip, &cfg->netmask, &cfg->gateway);
	} else {
		log_msg(LOG_INFO, "IP: DHCP");
	}
	netif_set_up(n);

	/* Get adapter MAC address */
	if ((res = cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, cyw43_mac))) {
		log_msg(LOG_ALERT, "Cannot get WiFi MAC address: %d", res);
		cyw43_arch_deinit();
		return;
	}
	log_msg(LOG_INFO, "WiFi MAC: %s", mac_address_str(cyw43_mac));

	/* Attempt to connect to a WiFi network... */
	if (cfg) {
		if (strlen(cfg->wifi_ssid) > 0 && strlen(cfg->wifi_passwd) > 0) {
			log_msg(LOG_INFO, "WiFi connecting to network: %s", cfg->wifi_ssid);
			res = cyw43_arch_wifi_connect_async(cfg->wifi_ssid,
							cfg->wifi_passwd,
							CYW43_AUTH_WPA2_AES_PSK);
			if (res != 0) {
				log_msg(LOG_ERR, "WiFi connect failed: %d", res);
				cyw43_arch_deinit();
				return;
			}
		}
	}

	wifi_initialized = true;

	/* Enable SNTP client... */
	sntp_init();
	if (!ip_addr_isany(&cfg->ntp_server)) {
		log_msg(LOG_INFO, "NTP Server: %s", ipaddr_ntoa(&cfg->ntp_server));
		sntp_setserver(0, &cfg->ntp_server);
	} else {
		log_msg(LOG_INFO, "NTP Server: DHCP");
		sntp_servermode_dhcp(1);
	}

	ip_addr_copy(syslog_server, cfg->syslog_server);

	httpd_init();
	http_set_ssi_handler(fanpico_ssi_handler, NULL, 0);
}


void wifi_status()
{
	int res;

	if (!wifi_initialized) {
		printf("0,,,\n");
		return;
	}

	res = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
	printf("%d,", res);

	struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
	printf("%s,", ipaddr_ntoa(netif_ip_addr4(n)));
	printf("%s,", ipaddr_ntoa(netif_ip_netmask4(n)));
	printf("%s\n", ipaddr_ntoa(netif_ip_gw4(n)));
}


void wifi_poll()
{
	static absolute_time_t test_t = 0;

	if (!wifi_initialized)
		return;


	cyw43_arch_poll();

	if (network_initialized) {
		if (time_passed(&test_t, 15000)) {
			syslog_msg(LOG_INFO, "test message: %llu", get_absolute_time());
		}
	}

}


/* LwIP DHCP hook to customize option 55 (Parameter-Request) when DHCP
 * client send DHCP_REQUEST message...
 */
void pico_dhcp_option_add_hook(struct netif *netif, struct dhcp *dhcp, u8_t state, struct dhcp_msg *msg,
			u8_t msg_type, u16_t *options_len_ptr)
{
	u8_t new_parameters[] = {
		7,   /* LOG */
		100, /* POSIX-TZ */
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


/* LwIP DHCP hook to parse additonal DHCP options received.
 */
void pico_dhcp_option_parse_hook(struct netif *netif, struct dhcp *dhcp, u8_t state, struct dhcp_msg *msg,
             u8_t msg_type, u8_t option, u8_t option_len, struct pbuf *pbuf, u16_t option_value_offset)
{
	static char timezone[64];
	ip4_addr_t log_ip;

	if (msg_type != DHCP_ACK)
		return;

	log_msg(LOG_DEBUG, "Parse DHCP option (msg=%02x): %u (len=%u,offset=%u)",
		msg_type, option, option_len, option_value_offset);

	if (option == 7 && option_len >= 4) {
		memcpy(&log_ip.addr, pbuf->payload + option_value_offset, 4);
		if (ip_addr_isany(&syslog_server)) {
			/* no syslog server configured, use one from DHCP... */
			ip_addr_copy(syslog_server, log_ip);
			log_msg(LOG_INFO, "Using Log Server from DHCP: %s", ip4addr_ntoa(&log_ip));
		} else {
			log_msg(LOG_INFO, "Ignoring Log Server from DHCP: %s", ip4addr_ntoa(&log_ip));
		}
	}
	else if (option == 100 && option_len > 0) {
		memcpy(timezone, pbuf->payload + option_value_offset, option_len);
		timezone[option_len] = 0;
		setenv("TZ", timezone, 1);
		tzset();
		log_msg(LOG_INFO, "Set (POSIX) Timezone from DHCP: %s", timezone);
	}
}

#endif /* WIFI_SUPPORT */


/****************************************************************************/


void pico_set_system_time(long int sec)
{
	datetime_t t;
	struct tm *ntp;
	time_t ntp_time = sec;

	if (!(ntp = localtime(&ntp_time)))
		return;

	rtc_set_datetime(tm_to_datetime(ntp, &t));

	log_msg(LOG_NOTICE, "SNTP Set System time: %s", asctime(ntp));
}

void network_init()
{
#ifdef WIFI_SUPPORT
	wifi_init();
#endif
}

void network_status()
{
#ifdef WIFI_SUPPORT
	wifi_status();
#endif
}


void network_poll()
{
#ifdef WIFI_SUPPORT
	wifi_poll();
#endif
}

void network_mac()
{
#ifdef WIFI_SUPPORT
	wifi_mac();
#endif
}

