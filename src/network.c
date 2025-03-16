/* network.c
   Copyright (C) 2022-2023 Timo Kokkonen <tjko@iki.fi>

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
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "pico/util/datetime.h"
#include "pico/aon_timer.h"
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

static absolute_time_t t_network_initialized;
static bool wifi_initialized = false;
static bool network_initialized = false;
static uint8_t cyw43_mac[6];
static char wifi_hostname[32];
static ip_addr_t syslog_server;
static ip_addr_t current_ip;

struct wifi_auth_type {
	char *name;
	uint32_t type;
};

static const struct wifi_auth_type auth_types[] = {
	{ "default",    CYW43_AUTH_WPA2_AES_PSK },

	{ "WPA3_WPA2",  CYW43_AUTH_WPA3_WPA2_AES_PSK },
	{ "WPA3",       CYW43_AUTH_WPA3_SAE_AES_PSK },
	{ "WPA2",       CYW43_AUTH_WPA2_AES_PSK },
	{ "WPA2_WPA",   CYW43_AUTH_WPA2_MIXED_PSK },
	{ "WPA",        CYW43_AUTH_WPA_TKIP_PSK },
	{ "OPEN",       CYW43_AUTH_OPEN },

	{ NULL, 0 }
};


bool wifi_get_auth_type(const char *name, uint32_t *type)
{
	int len, i;

	if (!name || !type)
		return false;

	/* Return default type if no match... */
	*type = auth_types[0].type;

	if ((len = strlen(name)) < 1)
		return false;

	i = 0;
	while (auth_types[i].name) {
		if (!strncasecmp(name, auth_types[i].name, len + 1)) {
			*type = auth_types[i].type;
			return true;
		}
		i++;
	}

	return false;
}

const char* wifi_auth_type_name(uint32_t type)
{
	int i = 1;

	while (auth_types[i].name) {
		if (auth_types[i].type == type) {
			return auth_types[i].name;
		}
		i++;
	}

	return "Unknown";
}

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
	if (netif_is_up(netif)) {
		log_msg(LOG_WARNING, "WiFi Status: UP (%s)", ipaddr_ntoa(netif_ip_addr4(netif)));
		ip_addr_set(&current_ip, netif_ip_addr4(netif));
	} else {
		log_msg(LOG_WARNING, "WiFi Status: DOWN");
	}

	if (netif_is_up(netif) && !network_initialized) {
		/* Network interface came up, first time... */
		syslog_open(&syslog_server, 0, LOG_USER, wifi_hostname);
		network_initialized = true;
		t_network_initialized = get_absolute_time();
	}
}

void wifi_init()
{
	uint32_t country_code = CYW43_COUNTRY_WORLDWIDE;
	struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
	uint32_t wifi_auth_mode;
	int res;

	memset(cyw43_mac, 0, sizeof(cyw43_mac));
	ip_addr_set_zero(&syslog_server);
	ip_addr_set_zero(&current_ip);
	wifi_hostname[0] = 0;

	log_msg(LOG_NOTICE, "Initializing WiFi...");

	/* If WiFi country is defined in configuratio, use it... */
	const char *country = cfg->wifi_country;
	if (strlen(country) > 1) {
		uint rev = 0;
		if (country[2] >= '0' && country[2] <= '9') {
			rev = country[2] - '0';
		}
		country_code = CYW43_COUNTRY(country[0], country[1], rev);
		log_msg(LOG_NOTICE, "WiFi country code: %06x (%s)",
			country_code, country);
	}

	if ((res = cyw43_arch_init_with_country(country_code))) {
		log_msg(LOG_ALERT, "WiFi initialization failed: %d", res);
		return;
	}

	cyw43_arch_enable_sta_mode();
	cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);

	cyw43_arch_lwip_begin();

	/* Set WiFi interface hostname... */
	if (strlen(cfg->hostname) > 0) {
		strncopy(wifi_hostname, cfg->hostname, sizeof(wifi_hostname));
	} else {
		snprintf(wifi_hostname, sizeof(wifi_hostname), "FanPico-%s", pico_serial_str());
	}
	log_msg(LOG_NOTICE, "WiFi hostname: %s", wifi_hostname);
	netif_set_hostname(n, wifi_hostname);

	/* Set callback for link/interface status changes */
	netif_set_link_callback(n, wifi_link_cb);
	netif_set_status_callback(n, wifi_status_cb);

	/* Configure interface */
	if (!ip_addr_isany(&cfg->ip)) {
		/* Configure static IP */
		dhcp_stop(n);
		log_msg(LOG_NOTICE, "     IP: %s", ipaddr_ntoa(&cfg->ip));
		log_msg(LOG_NOTICE, "Netmask: %s", ipaddr_ntoa(&cfg->netmask));
		log_msg(LOG_NOTICE, "Gateway: %s", ipaddr_ntoa(&cfg->gateway));
		netif_set_addr(n, &cfg->ip, &cfg->netmask, &cfg->gateway);
	} else {
		/* Use DHCP (default) */
		log_msg(LOG_NOTICE, "IP: DHCP");
	}
	netif_set_up(n);

	cyw43_arch_lwip_end();


	/* Get adapter MAC address */
	if ((res = cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, cyw43_mac))) {
		log_msg(LOG_ALERT, "Cannot get WiFi MAC address: %d", res);
		return;
	}
	log_msg(LOG_NOTICE, "WiFi MAC: %s", mac_address_str(cyw43_mac));
	wifi_get_auth_type(cfg->wifi_auth_mode, &wifi_auth_mode);
	log_msg(LOG_INFO, "WiFi Authentication mode: %s",
		wifi_auth_type_name(wifi_auth_mode));

	/* Attempt to connect to a WiFi network... */
	if (strlen(cfg->wifi_ssid) > 0) {
		if (strlen(cfg->wifi_passwd) < 1 && wifi_auth_mode != CYW43_AUTH_OPEN) {
			log_msg(LOG_ERR, "No WiFi Password configured.");
			return;
		}
		log_msg(LOG_NOTICE, "WiFi connecting to network: %s", cfg->wifi_ssid);
		if (cfg->wifi_mode == 0) {
			/* Default is to initiate asynchronous connection. */
			res = cyw43_arch_wifi_connect_async(cfg->wifi_ssid,
							cfg->wifi_passwd, wifi_auth_mode);
		} else {
			/* If "SYS:WIFI:MODE 1" has been used, attempt synchronous connection
			   as connection to some buggy(?) APs don't seem to always work with
			   asynchronouse connection... */
			int retries = 2;

			do {
				res = cyw43_arch_wifi_connect_timeout_ms(cfg->wifi_ssid,
									cfg->wifi_passwd,
									wifi_auth_mode,	30000);
				log_msg(LOG_INFO, "cyw43_arch_wifi_connect_timeout_ms(): %d", res);
			} while (res != 0 && --retries > 0);
		}
		if (res != 0) {
			log_msg(LOG_ERR, "WiFi connect failed: %d", res);
			return;
		}
	} else {
		log_msg(LOG_ERR, "No WiFi SSID configured");
		return;
	}

	wifi_initialized = true;

	cyw43_arch_lwip_begin();

	/* Enable SNTP client... */
	sntp_init();
	if (!ip_addr_isany(&cfg->ntp_server)) {
		log_msg(LOG_NOTICE, "NTP Server: %s", ipaddr_ntoa(&cfg->ntp_server));
		sntp_setserver(0, &cfg->ntp_server);
	} else {
		log_msg(LOG_NOTICE, "NTP Server: DHCP");
		sntp_servermode_dhcp(1);
	}

	/* Enable HTTP server */
	httpd_init();
#if TLS_SUPPORT
	struct altcp_tls_config *tls_config = tls_server_config();
	if (tls_config) {
		log_msg(LOG_NOTICE, "HTTPS/TLS enabled");
		httpd_inits(tls_config);
	}
#endif
	http_set_ssi_handler(fanpico_ssi_handler, NULL, 0);

	/* Enable Telnet server */
	if (cfg->telnet_active) {
		log_msg(LOG_NOTICE, "Telnet Server enabled");
		telnetserver_init();
	}

	/* Enable SNMP agent */
	if (cfg->snmp_active) {
		log_msg(LOG_NOTICE,"SNMP Agent enabled");
		fanpico_snmp_init();
	}

	cyw43_arch_lwip_end();

	ip_addr_copy(syslog_server, cfg->syslog_server);
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
	static absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(test_t, 0);
	static absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(publish_status_t, 0);
	static absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(publish_temp_t, 0);
	static absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(publish_vsensor_t, 0);
	static absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(publish_rpm_t, 0);
	static absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(publish_duty_t, 0);
	static absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(command_t, 0);
	static absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(reconnect_t, 0);
	static bool init_msg_sent = false;

	if (!wifi_initialized)
		return;

#if PICO_CYW43_ARCH_POLL
	cyw43_arch_poll();
#endif

	if (!network_initialized)
		return;

	if (!init_msg_sent) {
		if (time_passed(&t_network_initialized, 2000)) {
			init_msg_sent = true;
			log_msg(LOG_INFO, "Network initialization complete.%s",
				(rebooted_by_watchdog ? " [Rebooted by watchdog]" : ""));
			if (strlen(cfg->mqtt_server) > 0) {
				fanpico_setup_mqtt_client();
			}
		}
	}
	if (time_passed(&test_t, 3600 * 1000)) {
		uint32_t secs = to_us_since_boot(get_absolute_time()) / 1000000;
		uint32_t mins =  secs / 60;
		uint32_t hours = mins / 60;
		uint32_t days = hours / 24;

		syslog_msg(LOG_INFO, "Uptime: %lu days %02lu:%02lu:%02lu%s",
			days, hours % 24, mins % 60, secs % 60,
			(rebooted_by_watchdog ? " [Rebooted by watchdog]" : ""));
	}
	if (fanpico_mqtt_client_active()) {
		/* Check for pending SCPI command received via MQTT */
		if (time_passed(&command_t, 500)) {
			fanpico_mqtt_scpi_command();
		}

		/* Publish status update to MQTT status topic */
		if (cfg->mqtt_status_interval > 0) {
			if (time_passed(&publish_status_t, cfg->mqtt_status_interval * 1000)) {
				fanpico_mqtt_publish();
			}
		}
		if (cfg->mqtt_temp_interval > 0) {
			if (time_passed(&publish_temp_t, cfg->mqtt_temp_interval * 1000)) {
				fanpico_mqtt_publish_temp();
			}
		}
		if (cfg->mqtt_vsensor_interval > 0) {
			if (time_passed(&publish_vsensor_t, cfg->mqtt_vsensor_interval * 1000)) {
				fanpico_mqtt_publish_vsensor();
			}
		}
		if (cfg->mqtt_rpm_interval > 0) {
			if (time_passed(&publish_rpm_t, cfg->mqtt_rpm_interval * 1000)) {
				fanpico_mqtt_publish_rpm();
			}
		}
		if (cfg->mqtt_duty_interval > 0) {
			if (time_passed(&publish_duty_t, cfg->mqtt_duty_interval * 1000)) {
				fanpico_mqtt_publish_duty();
			}
		}

		if (time_passed(&reconnect_t, 1000)) {
			fanpico_mqtt_reconnect();
		}
	}

}

const char* wifi_ip()
{
	if (ip_addr_isany(&current_ip))
		return NULL;

	return ipaddr_ntoa(&current_ip);
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


/* LwIP DHCP hook to parse additional DHCP options received.
 */
void pico_dhcp_option_parse_hook(struct netif *netif, struct dhcp *dhcp, u8_t state, struct dhcp_msg *msg,
             u8_t msg_type, u8_t option, u8_t option_len, struct pbuf *pbuf, u16_t option_value_offset)
{
	char dhcp_timezone[64];
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
		int  len = (option_len < sizeof(dhcp_timezone) ? option_len : sizeof(dhcp_timezone) - 1);
		memcpy(dhcp_timezone, pbuf->payload + option_value_offset, len);
		dhcp_timezone[len] = 0;
		if (strlen(cfg->timezone) < 1) {
			log_msg(LOG_INFO, "Set (POSIX) Timezone from DHCP: %s", dhcp_timezone);
			setenv("TZ", dhcp_timezone, 1);
			tzset();
		} else {
			log_msg(LOG_INFO, "Ignore (POSIX) Timezone from DHCP: %s", dhcp_timezone);
		}
	}
}

#endif /* WIFI_SUPPORT */


/****************************************************************************/


void pico_set_system_time(long int sec)
{
	struct timespec ts;
	struct tm *ntp;
	time_t ntp_time = sec;


	if (!(ntp = localtime(&ntp_time)))
		return;

	time_t_to_timespec(ntp_time, &ts);
	if (aon_timer_is_running()) {
		aon_timer_set_time(&ts);
	} else {
		aon_timer_start(&ts);
	}

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


const char *network_ip()
{
#ifdef WIFI_SUPPORT
	return wifi_ip();
#else
	return NULL;
#endif
}


const char *network_hostname()
{
#ifdef WIFI_SUPPORT
	return wifi_hostname;
#else
	return "";
#endif
}
