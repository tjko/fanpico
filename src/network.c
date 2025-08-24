/* network.c
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
#include "pico/stdlib.h"
#include "pico/util/datetime.h"
#include "pico/aon_timer.h"
#ifdef LIB_PICO_CYW43_ARCH
#include "pico/cyw43_arch.h"
#include "lwip/dns.h"
#include "lwip/apps/sntp.h"
#include "lwip/apps/httpd.h"
#include "util_net.h"
#endif

#include "fanpico.h"


#ifdef WIFI_SUPPORT


#include "syslog.h"

#define WIFI_REJOIN_DELAY 10000 // Delay before attempting to re-join to WiFi (ms)
#define FANPICO_WIFI_INACTIVE -255

uint16_t fanpico_http_server_port = HTTP_SERVER_DEFAULT_PORT;
uint16_t fanpico_https_server_port = HTTPS_SERVER_DEFAULT_PORT;

static absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(t_network_initialized, 0);
static bool wifi_initialized = false;
static bool network_initialized = false;

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

const char* wifi_link_status_text(int status)
{
	switch (status) {
	case CYW43_LINK_DOWN:
		return "Link Down";
	case CYW43_LINK_JOIN:
		return "Joining";
	case CYW43_LINK_NOIP:
		return "No IP";
	case CYW43_LINK_UP:
		return "Link Up";
	case CYW43_LINK_FAIL:
		return "Link Fail";
	case CYW43_LINK_NONET:
		return "Network Fail";
	case CYW43_LINK_BADAUTH:
		return "Auth Fail";

	case FANPICO_WIFI_INACTIVE:
		return "<Inactive>";
	}

	return "Unknown";
}


void wifi_rejoin()
{
	int res;
	uint32_t wifi_auth_mode;


	if (!wifi_initialized)
		return;

	net_state->wifi_status_change = get_absolute_time();

	if (strlen(cfg->wifi_ssid) < 1)
		return;
	wifi_get_auth_type(cfg->wifi_auth_mode, &wifi_auth_mode);
	if (wifi_auth_mode != CYW43_AUTH_OPEN && strlen(cfg->wifi_passwd) < 1)
		return;

	/* Disconnect if currently joined to a network */
	res = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
	if (res == CYW43_LINK_JOIN || res == CYW43_LINK_NOIP || res == CYW43_LINK_UP) {
		if ((res = cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA))) {
			log_msg(LOG_ERR, "WiFi leave from network failed: %d", res);
		}
	}

	/* Initiate asynchronous join to the network */
	log_msg(LOG_NOTICE, "Attempt rejoining to WiFi network: %s", cfg->wifi_ssid);
	res = cyw43_arch_wifi_connect_async(cfg->wifi_ssid, cfg->wifi_passwd, wifi_auth_mode);
	if (res != 0) {
		log_msg(LOG_ERR, "WiFi rejoin failed: %d", res);
		return;
	}

}


static bool wifi_check_status()
{
	int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);

	if (status != net_state->wifi_status) {
		log_msg(LOG_NOTICE, "WiFi Status: %s ", wifi_link_status_text(status));
		net_state->wifi_status = status;
		net_state->wifi_status_change = get_absolute_time();
	} else {
		/* Check if should try rejoining to the network... */
		if (status != CYW43_LINK_UP) {
			uint32_t delay = WIFI_REJOIN_DELAY;

			if (status == CYW43_LINK_JOIN || status == CYW43_LINK_NOIP)
				delay *= 3;

			if (time_elapsed(net_state->wifi_status_change, delay))
				return true;
		}
	}

	return false;
}

static void wifi_link_cb(struct netif *netif)
{
	wifi_check_status();
	log_msg(LOG_WARNING, "Network Link: %s", (netif_is_link_up(netif) ? "UP" : "DOWN"));
}

static void wifi_status_cb(struct netif *netif)
{
	wifi_check_status();
	if (netif_is_up(netif)) {
		log_msg(LOG_WARNING, "Network Interface: UP (%s)", ipaddr_ntoa(netif_ip_addr4(netif)));
		net_state->netif_up = true;
		ip_addr_set(&net_state->ip, netif_ip_addr4(netif));
		ip_addr_set(&net_state->netmask, netif_ip_netmask4(netif));
		ip_addr_set(&net_state->gateway, netif_ip_gw4(netif));
		for (int i = 0; i < DNS_MAX_SERVERS; i++) {
			const ip_addr_t *s = dns_getserver(i);
			if (s)
				ip_addr_set(&net_state->dns_servers[i], s);
			else
				ip_addr_set_zero(&net_state->dns_servers[i]);
		}
		for (int i = 0; i < SNTP_MAX_SERVERS; i++) {
			const ip_addr_t *s = sntp_getserver(i);
			if (s)
				ip_addr_set(&net_state->ntp_servers[i], s);
			else
				ip_addr_set_zero(&net_state->ntp_servers[i]);
		}
	} else {
		log_msg(LOG_WARNING, "Network Interface: DOWN");
		net_state->netif_up = false;
	}

	if (netif_is_up(netif) && !network_initialized) {
		/* Network interface came up, first time... */
		if (cfg->syslog_active && !ip_addr_isany(&net_state->syslog_server)) {
			syslog_open(&net_state->syslog_server, 0, LOG_USER, net_state->hostname);
			log_msg(LOG_NOTICE, "Syslog client enabled (server: %s)",
				ipaddr_ntoa(&net_state->syslog_server));
		}
		network_initialized = true;
		t_network_initialized = get_absolute_time();
	}
}

static void wifi_init()
{
	uint32_t country_code = CYW43_COUNTRY_WORLDWIDE;
	struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
	uint32_t wifi_auth_mode;
	int res;

	memset(net_state->mac, 0, sizeof(net_state->mac));
	ip_addr_set_zero(&net_state->ip);
	ip_addr_set_zero(&net_state->netmask);
	ip_addr_set_zero(&net_state->gateway);
	net_state->hostname[0] = 0;
	net_state->netif_up = false;
	net_state->wifi_status = FANPICO_WIFI_INACTIVE;

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
		strncopy(net_state->hostname, cfg->hostname, sizeof(net_state->hostname));
	} else {
		snprintf(net_state->hostname, sizeof(net_state->hostname), "FanPico-%s", pico_serial_str());
	}
	log_msg(LOG_NOTICE, "WiFi hostname: %s", net_state->hostname);
	netif_set_hostname(n, net_state->hostname);

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
		log_msg(LOG_NOTICE,      "IP: DHCP");
	}
	netif_set_up(n);

	if (!ip_addr_isany(&cfg->dns_servers[0])) {
		for (int i = 0; i < DNS_MAX_SERVERS; i++) {
			const ip_addr_t *ip = &cfg->dns_servers[i];

			if (!ip_addr_isany(ip)) {
				log_msg(LOG_NOTICE, "DNS Server #%d: %s", i + 1,
					ipaddr_ntoa(ip));
				dns_setserver(i, ip);
			}
		}
	}

	cyw43_arch_lwip_end();


	/* Get adapter MAC address */
	if ((res = cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, net_state->mac))) {
		log_msg(LOG_ALERT, "Cannot get WiFi MAC address: %d", res);
		return;
	}
	log_msg(LOG_NOTICE, "WiFi MAC: %s", mac_address_str(net_state->mac));
	wifi_get_auth_type(cfg->wifi_auth_mode, &wifi_auth_mode);
	log_msg(LOG_NOTICE, "WiFi Authentication mode: %s",
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
									wifi_auth_mode,	10000);
				log_msg(LOG_NOTICE, "cyw43_arch_wifi_connect_timeout_ms(): %d", res);
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
	if (cfg->ntp_active) {
		if (!ip_addr_isany(&cfg->ntp_servers[0])) {
			sntp_servermode_dhcp(0);
			for (int i = 0; i < SNTP_MAX_SERVERS; i++) {
				const ip_addr_t *ip = &cfg->ntp_servers[i];

				if (!ip_addr_isany(ip)) {
					log_msg(LOG_NOTICE, "NTP Server #%d: %s", i + 1,
						ipaddr_ntoa(ip));
					sntp_setserver(i, ip);
				}
				ip_addr_copy(net_state->ntp_servers[i], *ip);
			}
		} else {
			log_msg(LOG_NOTICE, "NTP Server: DHCP");
			sntp_servermode_dhcp(1);
		}
		sntp_init();
	}

	/* Enable HTTP server */
	if (cfg->http_active) {
		log_msg(LOG_NOTICE, "HTTP Server enabled");
		if (cfg->http_port > 0)
			fanpico_http_server_port = cfg->http_port;
		if (cfg->https_port > 0)
			fanpico_https_server_port = cfg->https_port;
		httpd_init();
#if TLS_SUPPORT
		struct altcp_tls_config *tls_config = tls_server_config();
		if (tls_config) {
			log_msg(LOG_NOTICE, "HTTPS/TLS enabled");
			httpd_inits(tls_config);
		}
#endif
		http_set_ssi_handler(fanpico_ssi_handler, NULL, 0);
	}

	/* Enable Telnet server */
	if (cfg->telnet_active) {
		log_msg(LOG_NOTICE, "Telnet Server enabled");
		telnetserver_init();
	}

	/* Enable SSH server */
	if (cfg->ssh_active) {
		log_msg(LOG_NOTICE, "SSH Server enabled");
		sshserver_init();
	}

	/* Enable SNMP agent */
	if (cfg->snmp_active) {
		log_msg(LOG_NOTICE,"SNMP Agent enabled");
		fanpico_snmp_init();
	}

	cyw43_arch_lwip_end();

	if (cfg->syslog_active)
		ip_addr_copy(net_state->syslog_server, cfg->syslog_server);
}


void wifi_status()
{
	int res;

	if (!wifi_initialized) {
		res = FANPICO_WIFI_INACTIVE;
	} else {
		res = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
	}
	printf("%s,", wifi_link_status_text(res));

	if (res != FANPICO_WIFI_INACTIVE) {
		struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
		printf("%s,", ipaddr_ntoa(netif_ip_addr4(n)));
		printf("%s,", ipaddr_ntoa(netif_ip_netmask4(n)));
		printf("%s,%d\n", ipaddr_ntoa(netif_ip_gw4(n)), res);
	} else {
		printf(",,,%d\n", res);
	}
}

void wifi_info_display()
{
	char buf[32];

	printf(" Network Link: %s\n", (net_state->netif_up ? "Up" : "Down"));
	uptime_to_str(buf, sizeof(buf),
		absolute_time_diff_us(net_state->wifi_status_change, get_absolute_time()),
		true);
	printf("  WiFi Status: %s (%s since last change)\n",
		wifi_link_status_text(net_state->wifi_status), buf);
	printf("  MAC Address: %s\n", mac_address_str(net_state->mac));
	printf("  DHCP Client: %s\n", (ip_addr_isany(&cfg->ip) ? "Enabled" : "Disabled"));
	printf("DHCP Hostname: %s\n", net_state->hostname);
	printf("\n");
	printf("   IP Address: %s\n", ipaddr_ntoa(&net_state->ip));
	printf("      Netmask: %s\n", ipaddr_ntoa(&net_state->netmask));
	printf("      Gateway: %s\n", ipaddr_ntoa(&net_state->gateway));
	printf("\n");
	printf("  DNS Servers: %s", ipaddr_ntoa(&net_state->dns_servers[0]));
	for (int i = 1; i < DNS_MAX_SERVERS; i++) {
		if (!ip_addr_isany(&net_state->dns_servers[i])) {
			printf(", %s", ipaddr_ntoa(&net_state->dns_servers[i]));
		}
	}
	printf("\n");
	printf("  NTP Servers: %s", ipaddr_ntoa(&net_state->ntp_servers[0]));
	for (int i = 1; i < SNTP_MAX_SERVERS; i++) {
		if (!ip_addr_isany(&net_state->ntp_servers[i])) {
			printf(", %s", ipaddr_ntoa(&net_state->ntp_servers[i]));
		}
	}
	printf("\n");
	printf("   Log Server: %s\n", ipaddr_ntoa(&net_state->syslog_server));

}

static void wifi_poll()
{
	static absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(test_t, 0);
	static absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(publish_status_t, 0);
	static absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(publish_temp_t, 0);
	static absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(publish_vsensor_t, 0);
	static absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(publish_rpm_t, 0);
	static absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(publish_duty_t, 0);
	static absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(command_t, 0);
	static absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(reconnect_t, 0);
	static absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(wifi_status_check_t, 0);
	static bool init_msg_sent = false;

	if (!wifi_initialized)
		return;

#if PICO_CYW43_ARCH_POLL
	cyw43_arch_poll();
#endif

	/* Periodically check for WiFi (link) status... */
	if (time_passed(&wifi_status_check_t, 1500)) {
		if (wifi_check_status()) {
			/* Attempt to rejoin if connection has been in failed state
			   for a while... */
			wifi_rejoin();
		}
	}

	if (!network_initialized)
		return;

	if (!init_msg_sent) {
		if (time_passed(&t_network_initialized, 2000)) {
			init_msg_sent = true;
			log_msg(LOG_INFO, "Network initialization complete.%s",
				(rebooted_by_watchdog ? " [Rebooted by watchdog]" : ""));
			if (cfg->snmp_active)
				fanpico_snmp_startup_trap(persistent_mem->warmstart);
			if (strlen(cfg->mqtt_server) > 0)
				fanpico_setup_mqtt_client();
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

static const char* wifi_ip()
{
	static char ip_str[16 + 1];

	if (ip_addr_isany(&net_state->ip))
		return NULL;

	return ipaddr_ntoa_r(&net_state->ip, ip_str, sizeof(ip_str));
}


void pico_set_system_time(long int sec)
{
	struct timespec ts;
	struct tm *ntp, ntp_r;
	time_t ntp_time = sec;


	if (!(ntp = localtime_r(&ntp_time, &ntp_r)))
		return;
	time_t_to_timespec(ntp_time, &ts);

	if (aon_timer_is_running()) {
		aon_timer_set_time(&ts);
	} else {
		aon_timer_start(&ts);
	}

	log_msg(LOG_NOTICE, "SNTP Set System time: %s", asctime(ntp));
}

#endif /* WIFI_SUPPORT */


/****************************************************************************/



void network_init()
{
#ifdef WIFI_SUPPORT
	wifi_init();
#endif
}

void network_poll()
{
#ifdef WIFI_SUPPORT
	wifi_poll();
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

