/* mqtt.c
   Copyright (C) 2023 Timo Kokkonen <tjko@iki.fi>

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
#include "hardware/rtc.h"
#include "pico/stdlib.h"
#include "cJSON.h"
#ifdef LIB_PICO_CYW43_ARCH
#include "pico/cyw43_arch.h"
#include "lwip/dns.h"
#include "lwip/apps/mqtt.h"
#if TLS_SUPPORT
#include "lwip/altcp_tls.h"
#endif
#endif

#include "fanpico.h"

#ifdef WIFI_SUPPORT

#define MQTTS_PORT 8883
#define MQTT_CMD_MAX_LEN 100

mqtt_client_t *mqtt_client = NULL;
ip_addr_t mqtt_server_ip = IPADDR4_INIT_BYTES(0, 0, 0, 0);
u16_t mqtt_server_port = 0;
int incoming_topic = 0;
char mqtt_scpi_cmd[MQTT_CMD_MAX_LEN];
bool mqtt_scpi_cmd_queued = false;
absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(t_mqtt_disconnect, 0);
u16_t mqtt_reconnect = 0;


void mqtt_connect(mqtt_client_t *client);


int process_mqtt_command(const char *cmd)
{
//	const struct fanpico_state *st = fanpico_state;
	char buf[MQTT_CMD_MAX_LEN], *saveptr, *tok;
	int output = 0;
	int pwr = -1;
	int pwm = -1;

	if (!cmd)
		return -1;

	strncopy(buf, cmd, sizeof(buf));

	if (!(tok = strtok_r(buf, ":", &saveptr)))
		return -2;

	if (!strncasecmp(tok, "ALL", 4)) {
		output = -1;
	} else {
		int tmp;
		if (str_to_int(tok, &tmp, 10)) {
			if (tmp >= 1 && tmp <= FAN_COUNT)
				output = tmp;
		}
	}
	if (output == 0)
		return -3;

	if ((tok = strtok_r(NULL, ":", &saveptr))) {
		if (!strncasecmp(tok, "ON", 3)) {
			pwr = 1;
		}
		else if (!strncasecmp(tok, "OFF", 4)) {
			pwr = 0;
		}
		else if (!strncasecmp(tok, "PWM", 4)) {
			if ((tok = strtok_r(NULL, ":", &saveptr))) {
				int tmp;
				if (str_to_int(tok, &tmp, 10)) {
					if (tmp >= 0 && tmp <= 100)
						pwm = tmp;
				}
			}
			if (pwm < 0)
				return -4;
		}
	}

	for (int i = 0; i < FAN_COUNT; i++) {
		if (output == i + 1 || output == -1) {
#if 0
			if (pwr >= 0)
				st->pwr[i] = pwr;
			if (pwm >= 0)
				st->pwm[i] = pwm;
#endif
		}
	}

	return 0;
}

static void mqtt_pub_request_cb(void *arg, err_t result)
{
	if (result != ERR_OK) {
		log_msg(LOG_NOTICE, "MQTT failed to publish to topic: %d", result);
	}
}

int mqtt_publish_message(const char *topic, const char *buf, u16_t buf_len,
			u8_t qos, u8_t retain)
{
	if (!topic || !buf || buf_len == 0)
		return -1;
	if (!mqtt_client)
		return -2;

	/* Check that MQTT Client is connected */
	cyw43_arch_lwip_begin();
	u8_t connected = mqtt_client_is_connected(mqtt_client);
	cyw43_arch_lwip_end();
	if (!connected)
		return -3;

	log_msg(LOG_INFO, "MQTT publish to %s: %u bytes.", topic, buf_len);

	/* Publish message to a MQTT topic */
	cyw43_arch_lwip_begin();
	err_t err = mqtt_publish(mqtt_client, topic, buf, buf_len,
				qos, retain, mqtt_pub_request_cb, NULL);
	cyw43_arch_lwip_end();
	if (err != ERR_OK) {
		log_msg(LOG_NOTICE, "mqtt_publish_message(): failed %d (topic=%s, buf_len=%u)",
			err, topic, buf_len);
	}
	return err;
}

char* json_response_message(const char *cmd, int result, const char *msg)
{
	char *buf;
	cJSON *json;

	if (!(json = cJSON_CreateObject()))
		goto panic;

	cJSON_AddItemToObject(json, "command", cJSON_CreateString(cmd));
	cJSON_AddItemToObject(json, "result", cJSON_CreateString(result == 0 ? "OK" : "ERROR"));
	cJSON_AddItemToObject(json, "message", cJSON_CreateString(msg));

	if (!(buf = cJSON_Print(json)))
		goto panic;
	cJSON_Delete(json);
	return buf;

panic:
	if (json)
		cJSON_Delete(json);
	return NULL;
}

void send_mqtt_command_response(const char *cmd, int result, const char *msg)
{
	char *buf = NULL;

	if (!cmd || !msg || !mqtt_client || strlen(cfg->mqtt_resp_topic) < 1)
		return;

	/* Generate status message */
	if (!(buf = json_response_message(cmd, result, msg))) {
		log_msg(LOG_WARNING,"json_response_message(): failed");
		return;
	}
	mqtt_publish_message(cfg->mqtt_resp_topic, buf, strlen(buf), 2, 0);
	free(buf);
}

static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{
	log_msg(LOG_DEBUG, "MQTT incoming publish at topic %s with total length %u",
		topic, (unsigned int)tot_len);
	if (!strncmp(topic, cfg->mqtt_cmd_topic, strlen(cfg->mqtt_cmd_topic) + 1)) {
		incoming_topic = 1;
	} else {
		incoming_topic = 0;
	}
}

static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
	char cmd[MQTT_CMD_MAX_LEN];
	u8_t *end, *p;
	u32_t l;
	int res, mode;

	log_msg(LOG_DEBUG, "MQTT incoming publish payload with length %d, flags %u\n",
		len, (unsigned int)flags);

	if (incoming_topic != 1)
		return;

	/* Search for a command we recognize */
	if ((p = memmem(data, len, "CMD:", 4))) {
		mode = 0;
		p += 4;
	}
	else if ((p = memmem(data, len, "SCPI:", 5))) {
		mode = 1;
		p += 5;
	}
	else {
		return;
	}

	l = len - (p - data);
	if (l < 1)
		return;

	if (!(end = memchr(p, ';', l))) {
		log_msg(LOG_INFO, "MQTT ignore non-terminated command.");
		return;
	}
	l = end - p;
	if (l >= sizeof(cmd))
		l = sizeof(cmd) - 1;
	memcpy(cmd, p, l);
	cmd[l] = 0;

	/* Process command */
	if (mode ==  1) {
		if (!cfg->mqtt_allow_scpi) {
			log_msg(LOG_NOTICE, "MQTT SCPI commands not allowed: '%s'", cmd);
			return;
		}
		if (mqtt_scpi_cmd_queued) {
			log_msg(LOG_NOTICE, "MQTT SCPI command queue full: '%s'", cmd);
			send_mqtt_command_response(cmd, 1, "SCPI command queue full");
		} else {
			log_msg(LOG_NOTICE, "MQTT SCPI command queued: '%s'", cmd);
			strncopy(mqtt_scpi_cmd, cmd, sizeof(mqtt_scpi_cmd));
			mqtt_scpi_cmd_queued = true;
		}
	} else {
		res = process_mqtt_command(cmd);
		if (res == 0) {
			log_msg(LOG_NOTICE, "MQTT command processed: '%s'", cmd);
			send_mqtt_command_response(cmd, res, "command successfull");
		}
		else {
			log_msg(LOG_NOTICE, "MQTT invalid command: '%s' (%d)", cmd, res);
			send_mqtt_command_response(cmd, res, "invalid command");
		}
	}

}

static void mqtt_sub_request_cb(void *arg, err_t result)
{
	if (result != ERR_OK) {
		log_msg(LOG_WARNING, "MQTT failed to subscribe command topic: %d", result);
	}
}

static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
	t_mqtt_disconnect = get_absolute_time();

	if (status == MQTT_CONNECT_ACCEPTED) {
		log_msg(LOG_INFO, "MQTT connected to %s:%u", ipaddr_ntoa(&mqtt_server_ip),
			mqtt_server_port);
		mqtt_set_inpub_callback(client, mqtt_incoming_publish_cb, mqtt_incoming_data_cb, arg);
		if (strlen(cfg->mqtt_cmd_topic) > 0) {
			log_msg(LOG_INFO, "MQTT subscribe to command topic: %s", cfg->mqtt_cmd_topic);
			err_t err = mqtt_subscribe(client, cfg->mqtt_cmd_topic, 1,
						mqtt_sub_request_cb, arg);
			if (err != ERR_OK) {
				log_msg(LOG_WARNING, "MQTT subscribe failed: %d", err);
			}
		}
	}
	else if (status == MQTT_CONNECT_DISCONNECTED) {
		log_msg(LOG_WARNING, "MQTT disconnected from %s", ipaddr_ntoa(&mqtt_server_ip));
		mqtt_reconnect = 5;
	}
	else if (status == MQTT_CONNECT_TIMEOUT) {
		log_msg(LOG_WARNING, "MQTT connect: timeout %s", ipaddr_ntoa(&mqtt_server_ip));
		mqtt_reconnect = 30;
	}
	else if (status == MQTT_CONNECT_REFUSED_USERNAME_PASS) {
		log_msg(LOG_WARNING, "MQTT connect: login failure");
	}
	else if (status == MQTT_CONNECT_REFUSED_NOT_AUTHORIZED_) {
		log_msg(LOG_WARNING, "MQTT connect: not authorized");
	}
	else {
		log_msg(LOG_WARNING, "MQTT connect: refused (%d)", status);
	}
}

static void mqtt_dns_resolve_cb(const char *name, const ip_addr_t *ipaddr, void *arg)
{
	t_mqtt_disconnect = get_absolute_time();

	if (ipaddr && ipaddr->addr) {
		log_msg(LOG_INFO, "DNS resolved: %s -> %s\n", name, ipaddr_ntoa(ipaddr));
		ip_addr_set(&mqtt_server_ip, ipaddr);
		mqtt_connect(mqtt_client);
	} else {
		log_msg(LOG_WARNING, "Failed to resolve MQTT server name: %s", name);
		mqtt_reconnect = 30;
	}
}

void mqtt_connect(mqtt_client_t *client)
{
	struct mqtt_connect_client_info_t ci;
	char client_id[32];
	uint16_t port = MQTT_PORT;
	err_t err;

	if (!client)
		return;

	/* Resolve domain name */
	cyw43_arch_lwip_begin();
	err = dns_gethostbyname(cfg->mqtt_server, &mqtt_server_ip, mqtt_dns_resolve_cb, NULL);
	cyw43_arch_lwip_end();
	if (err != ERR_OK) {
		if (err == ERR_INPROGRESS)
			log_msg(LOG_INFO, "Resolving DNS name: %s\n", cfg->mqtt_server);
		else
			log_msg(LOG_WARNING, "Failed to resolve MQTT server name: %s (%d)" ,
				cfg->mqtt_server, err);
		return;
	}

	/* Setup client connect info */
	memset(&ci, 0, sizeof(ci));
	snprintf(client_id, sizeof(client_id), "fanpico-%s_%s", FANPICO_BOARD, pico_serial_str());
	ci.client_id = client_id;
	ci.client_user = cfg->mqtt_user;
	ci.client_pass = cfg->mqtt_pass;
	ci.keep_alive = 60;
	ci.will_topic = NULL;
	ci.will_msg = NULL;
	ci.will_retain = 0;
	ci.will_qos = 0;
#if TLS_SUPPORT
	if (cfg->mqtt_tls) {
		cyw43_arch_lwip_begin();
		ci.tls_config = altcp_tls_create_config_client(NULL, 0);
		cyw43_arch_lwip_end();
		port = MQTTS_PORT;;
	}
#endif
	if (cfg->mqtt_port > 0)
		port = cfg->mqtt_port;
	mqtt_server_port = port;

	/* Connect to MQTT Server */
	log_msg(LOG_INFO, "MQTT Connecting to %s:%u%s",	cfg->mqtt_server, mqtt_server_port,
		cfg->mqtt_tls ? " (TLS)" : "");
	cyw43_arch_lwip_begin();
	err = mqtt_client_connect(mqtt_client, &mqtt_server_ip, mqtt_server_port,
				mqtt_connection_cb, 0, &ci);
	cyw43_arch_lwip_end();
	if (err != ERR_OK) {
		log_msg(LOG_WARNING,"mqtt_client_connect(): failed %d", err);
	}
}

void fanpico_setup_mqtt_client()
{
	ip_addr_set_zero(&mqtt_server_ip);

	cyw43_arch_lwip_begin();
	mqtt_client = mqtt_client_new();
	cyw43_arch_lwip_end();
	if (!mqtt_client) {
		log_msg(LOG_WARNING,"mqtt_client_new(): failed");
		return;
	}

	mqtt_connect(mqtt_client);
}

int fanpico_mqtt_client_active()
{
	return (mqtt_client == NULL ? 0 : 1);
}

void fanpico_mqtt_reconnect()
{
	if (mqtt_reconnect == 0)
		return;

	if (time_passed(&t_mqtt_disconnect, mqtt_reconnect * 1000)) {
		log_msg(LOG_INFO, "MQTT attempt reconnecting to server");
		mqtt_reconnect = 0;
		mqtt_connect(mqtt_client);
	}
}

char* json_status_message()
{
	const struct fanpico_state *st = fanpico_state;
	char *buf;
	cJSON *json, *outputs, *o;
	int i;

	if (!(json = cJSON_CreateObject()))
		goto panic;

	cJSON_AddItemToObject(json, "name", cJSON_CreateString(cfg->name));
	cJSON_AddItemToObject(json, "hostname", cJSON_CreateString(network_hostname()));
	if (network_ip())
		cJSON_AddItemToObject(json, "ip", cJSON_CreateString(network_ip()));
	if (!(outputs = cJSON_CreateArray()))
		goto panic;
	cJSON_AddItemToObject(json, "fans", outputs);
	for (i = 0; i < FAN_COUNT; i++) {
		if (!(o = cJSON_CreateObject()))
			goto panic;
		cJSON_AddItemToObject(o, "id", cJSON_CreateNumber(i + 1));
		cJSON_AddItemToObject(o, "pwm", cJSON_CreateNumber(round_decimal(st->fan_duty[i], 1)));
		cJSON_AddItemToObject(o, "freq", cJSON_CreateNumber(round_decimal(st->fan_freq[i], 1)));
		cJSON_AddItemToArray(outputs, o);
	}

	if (!(buf = cJSON_Print(json)))
		goto panic;
	cJSON_Delete(json);
	return buf;

panic:
	if (json)
		cJSON_Delete(json);
	return NULL;
}

void fanpico_mqtt_publish()
{
	char *buf = NULL;

	if (!mqtt_client || strlen(cfg->mqtt_status_topic) < 1)
		return;

	/* Generate status message */
	if (!(buf = json_status_message())) {
		log_msg(LOG_WARNING,"json_status_message(): failed");
		return;
	}
	mqtt_publish_message(cfg->mqtt_status_topic, buf, strlen(buf), 2 , 0);
	free(buf);
}

void fanpico_mqtt_scpi_command()
{
	const struct fanpico_state *st = fanpico_state;
	char cmd[MQTT_CMD_MAX_LEN];
	int res;

	if (!mqtt_client || !mqtt_scpi_cmd_queued)
		return;

	strncopy(cmd, mqtt_scpi_cmd, sizeof(cmd));
	process_command(st, (struct fanpico_config *)cfg, cmd);
	if ((res = last_command_status()) == 0) {
		log_msg(LOG_INFO, "MQTT SCPI command successfull: '%s'", mqtt_scpi_cmd);
		send_mqtt_command_response(mqtt_scpi_cmd, res, "SCPI command successfull");
	} else {
		log_msg(LOG_NOTICE, "MQTT SCPI command failed: '%s' (%d)", mqtt_scpi_cmd, res);
		if (res == -113)
			send_mqtt_command_response(mqtt_scpi_cmd, res, "SCPI unknown command");
		else
			send_mqtt_command_response(mqtt_scpi_cmd, res, "SCPI command failed");
	}

	mqtt_scpi_cmd[0] = 0;
	mqtt_scpi_cmd_queued = false;
	//update_core1_state();
}

#endif /* WIFI_SUPPORT */
