/* mqtt.c
   Copyright (C) 2023-2024 Timo Kokkonen <tjko@iki.fi>

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

#define MQTT_CMD_MAX_LEN 100

mqtt_client_t *mqtt_client = NULL;
ip_addr_t mqtt_server_ip = IPADDR4_INIT_BYTES(0, 0, 0, 0);
u16_t mqtt_server_port = 0;
int incoming_topic = 0;
int mqtt_qos = 1;
char mqtt_ha_birth_topic[64 + 1];
char mqtt_ha_base_topic[64 + 1];
char mqtt_scpi_cmd[MQTT_CMD_MAX_LEN];
bool mqtt_scpi_cmd_queued = false;
absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(t_mqtt_disconnect, 0);
absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(t_mqtt_ha_discovery, 0);
u16_t mqtt_reconnect = 0;
u16_t mqtt_ha_discovery = 0;



static void mqtt_connect(mqtt_client_t *client);



static void mqtt_pub_request_cb(void *arg, err_t result)
{
	const char *topic = (const char*)arg;

	if (!topic)
		topic = "NULL";

	if (result == ERR_OK) {
		log_msg(LOG_DEBUG, "MQTT publish successful (%s)", topic);
	}
	else if (result == ERR_TIMEOUT) {
		log_msg(LOG_NOTICE, "MQTT publish failed: timeout (%s)", topic);
	}
	else {
		log_msg(LOG_NOTICE, "MQTT publish failed: %d (%s)", result, topic);
	}
}

static int mqtt_publish_message(const char *topic, const char *buf, u16_t buf_len,
			u8_t qos, u8_t retain, const char *arg)
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
				qos, retain, mqtt_pub_request_cb, (void*)arg);
	cyw43_arch_lwip_end();
	if (err != ERR_OK) {
		log_msg(LOG_NOTICE, "mqtt_publish_message(): failed %d (topic=%s, buf_len=%u)",
			err, topic, buf_len);
	}
	return err;
}

static char* json_response_message(const char *cmd, int result, const char *msg)
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

static void send_mqtt_command_response(const char *cmd, int result, const char *msg)
{
	char *buf = NULL;

	if (!cmd || !msg || !mqtt_client || strlen(cfg->mqtt_resp_topic) < 1)
		return;

	/* Generate status message */
	if (!(buf = json_response_message(cmd, result, msg))) {
		log_msg(LOG_WARNING,"json_response_message(): failed");
		return;
	}
	mqtt_publish_message(cfg->mqtt_resp_topic, buf, strlen(buf), mqtt_qos, 0,
			cfg->mqtt_resp_topic);
	free(buf);
}

static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{
	log_msg(LOG_DEBUG, "MQTT incoming publish at topic %s with total length %u",
		topic, (unsigned int)tot_len);
	if (!strncmp(topic, cfg->mqtt_cmd_topic, strlen(cfg->mqtt_cmd_topic) + 1)) {
		incoming_topic = 1;
	} else if (strlen(mqtt_ha_birth_topic) > 0) {
		if (!strncmp(topic, mqtt_ha_birth_topic, strlen(mqtt_ha_birth_topic) + 1)) {
			incoming_topic = 2;
		}
	} else {
		log_msg(LOG_DEBUG, "Ignoring publish on unknown topic: %s", topic);
		incoming_topic = 0;
	}
}

static void incoming_fanpico_cmd(const u8_t *data, u16_t len)
{
	char cmd[MQTT_CMD_MAX_LEN];
	const u8_t *end, *start;
	int l;


	/* Check for command prefix, if found skip past prefix */
	if ((start = memmem(data, len, "CMD:", 4))) {
		start += 4;
		l = len - (start - data);
		if (l < 1)
			return;
	} else {
		start = data;
		l = len;
	}
	/* Check for command suffix, if found ignore anything past it */
	if (!(end = memchr(start, ';', l)))
		end = start + l;
	if ((l = end - start) < 1)
		return;
	if (l >= sizeof(cmd))
		l = sizeof(cmd) - 1;
	memcpy(cmd, start, l);
	cmd[l] = 0;


	/* Check if should be command allowed */
	if (!cfg->mqtt_allow_scpi) {
		if (strncasecmp(cmd, "WRITE:", 6)) {
			log_msg(LOG_NOTICE, "MQTT SCPI commands not allowed: '%s'", cmd);
			return;
		}
	}

	if (mqtt_scpi_cmd_queued) {
		log_msg(LOG_NOTICE, "MQTT SCPI command queue full: '%s'", cmd);
		send_mqtt_command_response(cmd, 1, "SCPI command queue full");
	} else {
		log_msg(LOG_NOTICE, "MQTT SCPI command queued: '%s'", cmd);
		strncopy(mqtt_scpi_cmd, cmd, sizeof(mqtt_scpi_cmd));
		mqtt_scpi_cmd_queued = true;
	}

}


static void incoming_ha_status(const u8_t *data, u16_t len)
{
	/* Handle incoming Home Assistant status messages (online/offline) */

	log_msg(LOG_INFO, "Home Assistant status message received: %d", len);

	if (memmem(data, len, "online", 6)) {
		log_msg(LOG_INFO, "Schedule resending HA MQTT Discovery messages");
		mqtt_ha_discovery = 1;
		t_mqtt_ha_discovery = get_absolute_time();
	}
}


static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
	log_msg(LOG_DEBUG, "MQTT incoming publish payload with length %d, flags %u\n",
		len, (unsigned int)flags);
	if (len < 1 || !data)
		return;

	if (incoming_topic == 1) {
		incoming_fanpico_cmd(data, len);
	}
	else if (incoming_topic == 2) {
		incoming_ha_status(data, len);
	}
}

static void mqtt_sub_request_cb(void *arg, err_t result)
{
	if (result != ERR_OK) {
		log_msg(LOG_WARNING, "MQTT failed to subscribe command topic: %d", result);
	}
}

static void mqtt_ha_sub_request_cb(void *arg, err_t result)
{
	if (result != ERR_OK) {
		log_msg(LOG_WARNING, "MQTT failed to subscribe HA Birth topic: %d", result);
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
			/* Subscribe to command topic */
			log_msg(LOG_INFO, "MQTT subscribe to command topic: %s", cfg->mqtt_cmd_topic);
			err_t err = mqtt_subscribe(client, cfg->mqtt_cmd_topic, 1,
						mqtt_sub_request_cb, arg);
			if (err != ERR_OK) {
				log_msg(LOG_WARNING, "MQTT subscribe failed: %d", err);
			}
		}
		if (strlen(cfg->mqtt_ha_discovery_prefix) > 0) {
			/* Subscribe to Home Assistant Birth topic */
			snprintf(mqtt_ha_birth_topic, sizeof(mqtt_ha_birth_topic), "%s/status",
				cfg->mqtt_ha_discovery_prefix);
			log_msg(LOG_INFO, "MQTT subscribe to HA Birth topic: %s", mqtt_ha_birth_topic);
			err_t err = mqtt_subscribe(client, mqtt_ha_birth_topic, 1, mqtt_ha_sub_request_cb, arg);
			if (err != ERR_OK) {
				log_msg(LOG_WARNING, "MQTT subscribe failed: %d", err);
			}

			snprintf(mqtt_ha_base_topic, sizeof(mqtt_ha_base_topic), "%s/sensor/fanpico_%s",
				cfg->mqtt_ha_discovery_prefix, pico_serial_str());
			mqtt_ha_discovery = 5;
			t_mqtt_ha_discovery = get_absolute_time();
		}
	}
	else if (status == MQTT_CONNECT_DISCONNECTED) {
		log_msg(LOG_WARNING, "MQTT disconnected from %s", ipaddr_ntoa(&mqtt_server_ip));
		mqtt_reconnect = 15;
	}
	else if (status == MQTT_CONNECT_TIMEOUT) {
		log_msg(LOG_WARNING, "MQTT connect: timeout (%s)", ipaddr_ntoa(&mqtt_server_ip));
		mqtt_reconnect = 30;
	}
	else if (status == MQTT_CONNECT_REFUSED_USERNAME_PASS) {
		log_msg(LOG_WARNING, "MQTT connect: login failure (%s)", ipaddr_ntoa(&mqtt_server_ip));
	}
	else if (status == MQTT_CONNECT_REFUSED_NOT_AUTHORIZED_) {
		log_msg(LOG_WARNING, "MQTT connect: not authorized (%s)", ipaddr_ntoa(&mqtt_server_ip));
		mqtt_reconnect = 180;
	}
	else {
		log_msg(LOG_WARNING, "MQTT connect: refused (status=%d) (%s)", status, ipaddr_ntoa(&mqtt_server_ip));
		mqtt_reconnect = 90;
	}
}

static void mqtt_dns_resolve_cb(const char *name, const ip_addr_t *ipaddr, void *arg)
{
	if (ipaddr && ipaddr->addr) {
		log_msg(LOG_INFO, "DNS resolved: %s -> %s\n", name, ipaddr_ntoa(ipaddr));
		ip_addr_set(&mqtt_server_ip, ipaddr);
		mqtt_connect(mqtt_client);
	} else {
		log_msg(LOG_WARNING, "Failed to resolve MQTT server name: %s", name);
		t_mqtt_disconnect = get_absolute_time();
		mqtt_reconnect = 30;
	}
}

static void mqtt_connect(mqtt_client_t *client)
{
#if TLS_SUPPORT
	static struct altcp_tls_config *tlsconfig = NULL;
#endif
	struct mqtt_connect_client_info_t ci;
	char client_id[32];
	uint16_t port = MQTT_PORT;
	err_t err;

	if (!client)
		return;

	mqtt_reconnect = 0;
	mqtt_ha_discovery = 0;

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
		if (!tlsconfig) {
			cyw43_arch_lwip_begin();
			tlsconfig = altcp_tls_create_config_client(NULL, 0);
			cyw43_arch_lwip_end();
			if (!tlsconfig)
				log_msg(LOG_WARNING, "altcp_tls_create_config_client(): failed");
		}
		ci.tls_config = tlsconfig;
		port = MQTT_TLS_PORT;
	}
#endif
	if (cfg->mqtt_port > 0)
		port = cfg->mqtt_port;
	mqtt_server_port = port;

	/* Connect to MQTT Server */
	log_msg(LOG_NOTICE, "MQTT Connecting to %s:%u%s", cfg->mqtt_server, mqtt_server_port,
		cfg->mqtt_tls ? " (TLS)" : "");
	cyw43_arch_lwip_begin();
	err = mqtt_client_connect(mqtt_client, &mqtt_server_ip, mqtt_server_port,
				mqtt_connection_cb, 0, &ci);
	cyw43_arch_lwip_end();
	if (err != ERR_OK) {
		log_msg(LOG_WARNING,"mqtt_client_connect(): failed %d", err);
		if (err == ERR_RTE) {
			t_mqtt_disconnect = get_absolute_time();
			mqtt_reconnect = 30;
		}
	}
}

void fanpico_setup_mqtt_client()
{
	if (mqtt_client)
		return;

	ip_addr_set_zero(&mqtt_server_ip);
	mqtt_reconnect = 0;
	mqtt_ha_discovery = 0;
	mqtt_ha_birth_topic[0] = 0;
	mqtt_ha_base_topic[0] = 0;
	mqtt_scpi_cmd[0] = 0;

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
		cyw43_arch_lwip_begin();
		u8_t connected = mqtt_client_is_connected(mqtt_client);
		cyw43_arch_lwip_end();
		if (connected) {
			log_msg(LOG_INFO, "MQTT attempt disconnecting existing connection");
			cyw43_arch_lwip_begin();
			mqtt_disconnect(mqtt_client);
			cyw43_arch_lwip_end();
		}
		mqtt_connect(mqtt_client);
	}
}


static char* json_ha_discovery_message(const char *type, int idx, int count)
{
	char *buf;
	cJSON *json, *d, *id;
	char tmp[100];

	if (!(json = cJSON_CreateObject()))
		return NULL;

	if (!strncmp(type, "sensor", 6)) {
		snprintf(tmp, sizeof(tmp), "Sensor: %s", cfg->sensors[idx - 1].name);
		cJSON_AddItemToObject(json, "name", cJSON_CreateString(tmp));
		cJSON_AddItemToObject(json, "device_class", cJSON_CreateString("temperature"));
		cJSON_AddItemToObject(json, "unit_of_measurement", cJSON_CreateString("°C"));
	}
	else if (!strncmp(type, "vtemp", 5)) {
		snprintf(tmp, sizeof(tmp), "Sensor: %s", cfg->vsensors[idx - 1].name);
		cJSON_AddItemToObject(json, "name", cJSON_CreateString(tmp));
		cJSON_AddItemToObject(json, "device_class", cJSON_CreateString("temperature"));
		cJSON_AddItemToObject(json, "unit_of_measurement", cJSON_CreateString("°C"));
	}
	else if (!strncmp(type, "vhumidity", 9)) {
		snprintf(tmp, sizeof(tmp), "Sensor: %s", cfg->vsensors[idx - 1].name);
		cJSON_AddItemToObject(json, "name", cJSON_CreateString(tmp));
		cJSON_AddItemToObject(json, "device_class", cJSON_CreateString("humidity"));
		cJSON_AddItemToObject(json, "unit_of_measurement", cJSON_CreateString("%"));
	}
	else if (!strncmp(type, "vpressure", 9)) {
		snprintf(tmp, sizeof(tmp), "Sensor: %s", cfg->vsensors[idx - 1].name);
		cJSON_AddItemToObject(json, "name", cJSON_CreateString(tmp));
		cJSON_AddItemToObject(json, "device_class", cJSON_CreateString("pressure"));
		cJSON_AddItemToObject(json, "unit_of_measurement", cJSON_CreateString("hPa"));
	}
	else if (!strncmp(type, "fanrpm", 6)) {
		snprintf(tmp, sizeof(tmp), "Fan: %s", cfg->fans[idx - 1].name);
		cJSON_AddItemToObject(json, "name", cJSON_CreateString(tmp));
		cJSON_AddItemToObject(json, "icon", cJSON_CreateString("mdi:fan"));
		cJSON_AddItemToObject(json, "unit_of_measurement", cJSON_CreateString("RPM"));
	}
	else if (!strncmp(type, "fanpwm", 6)) {
		snprintf(tmp, sizeof(tmp), "Fan: %s", cfg->fans[idx - 1].name);
		cJSON_AddItemToObject(json, "name", cJSON_CreateString(tmp));
		cJSON_AddItemToObject(json, "icon", cJSON_CreateString("mdi:speedometer"));
		cJSON_AddItemToObject(json, "unit_of_measurement", cJSON_CreateString("%"));
	}
	else if (!strncmp(type, "mbfanrpm", 8)) {
		snprintf(tmp, sizeof(tmp), "MB Fan: %s", cfg->mbfans[idx - 1].name);
		cJSON_AddItemToObject(json, "name", cJSON_CreateString(tmp));
		cJSON_AddItemToObject(json, "icon", cJSON_CreateString("mdi:fan"));
		cJSON_AddItemToObject(json, "unit_of_measurement", cJSON_CreateString("RPM"));
	}
	else if (!strncmp(type, "mbfanpwm", 8)) {
		snprintf(tmp, sizeof(tmp), "MB Fan: %s", cfg->mbfans[idx - 1].name);
		cJSON_AddItemToObject(json, "name", cJSON_CreateString(tmp));
		cJSON_AddItemToObject(json, "icon", cJSON_CreateString("mdi:speedometer"));
		cJSON_AddItemToObject(json, "unit_of_measurement", cJSON_CreateString("%"));
	}
	else {
		goto panic;
	}

	snprintf(tmp, sizeof(tmp), "{{ value_json.%s%d }}", type, idx);
	cJSON_AddItemToObject(json, "value_template", cJSON_CreateString(tmp));
	snprintf(tmp, sizeof(tmp), "%s/state", mqtt_ha_base_topic);
	cJSON_AddItemToObject(json, "state_topic", cJSON_CreateString(tmp));
	snprintf(tmp, sizeof(tmp), "fanpico_%s_%s%d", pico_serial_str(), type, idx);
	cJSON_AddItemToObject(json, "unique_id", cJSON_CreateString(tmp));

	if (!(d = cJSON_CreateObject()))
		goto panic;
	if (!(id = cJSON_CreateArray()))
		goto panic;

	snprintf(tmp, sizeof(tmp), "fanpico_%s", pico_serial_str());
	cJSON_AddItemToArray(id, cJSON_CreateString(tmp));
	cJSON_AddItemToObject(d, "identifiers", id);
	if (count == 0) {
		cJSON_AddItemToObject(d, "name", cJSON_CreateString(cfg->name));
		cJSON_AddItemToObject(d, "manufacturer", cJSON_CreateString("TJKO Industries"));
		cJSON_AddItemToObject(d, "model", cJSON_CreateString("FanPico"));
		cJSON_AddItemToObject(d, "model_id", cJSON_CreateString(FANPICO_MODEL));
		cJSON_AddItemToObject(d, "serial_number", cJSON_CreateString(pico_serial_str()));
		cJSON_AddItemToObject(d, "sw_version", cJSON_CreateString(FANPICO_VERSION));
	}
	cJSON_AddItemToObject(json, "device", d);

	if (!(buf = cJSON_Print(json)))
		goto panic;
	cJSON_Delete(json);
	return buf;

panic:
	cJSON_Delete(json);
	return NULL;
}

static int send_ha_discovery_msg(const char *topic, const char *type, int idx, int count)
{
	char *buf;

	if (!(buf = json_ha_discovery_message(type, idx, count))) {
		log_msg(LOG_WARNING, "json_ha_discovery_message(): failed");
		return 1;
	}
	log_msg(LOG_INFO, "Publish HA Discovery Message: %s (%d)", topic, strlen(buf));
	mqtt_publish_message(topic, buf, strlen(buf), mqtt_qos, 0, topic);
	free(buf);

	return 0;
}

static void fanpico_mqtt_ha_discovery()
{
	char topic[128];
	int count = 0;

	if (mqtt_ha_discovery == 0)
		return;

	if (!time_passed(&t_mqtt_ha_discovery, mqtt_ha_discovery * 1000))
		return;

	mqtt_ha_discovery = 0;

	for (int i = 0; i < SENSOR_COUNT; i++) {
		if (cfg->mqtt_temp_mask & (1 << i)) {
			snprintf(topic, sizeof(topic), "%s_t%d/config", mqtt_ha_base_topic, i + 1);
			if (send_ha_discovery_msg(topic, "sensor", i + 1, count++))
				return;
		}
	}

	for (int i = 0; i < VSENSOR_COUNT; i++) {
		if (cfg->mqtt_vtemp_mask & (1 << i)) {
			snprintf(topic, sizeof(topic), "%s_vt%d/config", mqtt_ha_base_topic, i + 1);
			if (send_ha_discovery_msg(topic, "vtemp", i + 1, count++))
				return;
		}
		if (cfg->mqtt_vhumidity_mask & (1 << i)) {
			snprintf(topic, sizeof(topic), "%s_vh%d/config", mqtt_ha_base_topic, i + 1);
			if (send_ha_discovery_msg(topic, "vhumidity", i + 1, count++))
				return;
		}
		if (cfg->mqtt_vpressure_mask & (1 << i)) {
			snprintf(topic, sizeof(topic), "%s_vp%d/config", mqtt_ha_base_topic, i + 1);
			if (send_ha_discovery_msg(topic, "vpressure", i + 1, count++))
				return;
		}
	}

	for (int i = 0; i < FAN_COUNT; i++) {
		if (cfg->mqtt_fan_rpm_mask & (1 << i)) {
			snprintf(topic, sizeof(topic), "%s_fr%d/config", mqtt_ha_base_topic, i + 1);
			if (send_ha_discovery_msg(topic, "fanrpm", i + 1, count++))
				return;
		}
		if (cfg->mqtt_fan_duty_mask & (1 << i)) {
			snprintf(topic, sizeof(topic), "%s_fp%d/config", mqtt_ha_base_topic, i + 1);
			if (send_ha_discovery_msg(topic, "fanpwm", i + 1, count++))
				return;
		}
	}

	for (int i = 0; i < MBFAN_COUNT; i++) {
		if (cfg->mqtt_mbfan_rpm_mask & (1 << i)) {
			snprintf(topic, sizeof(topic), "%s_mfr%d/config", mqtt_ha_base_topic, i + 1);
			if (send_ha_discovery_msg(topic, "mbfanrpm", i + 1, count++))
				return;
		}
		if (cfg->mqtt_mbfan_duty_mask & (1 << i)) {
			snprintf(topic, sizeof(topic), "%s_mfp%d/config", mqtt_ha_base_topic, i + 1);
			if (send_ha_discovery_msg(topic, "mbfanpwm", i + 1, count++))
				return;
		}
	}
}

static char* json_ha_state_message()
{
	const struct fanpico_state *st = fanpico_state;
	cJSON *json;
	char *buf, name[32];

	if (!(json = cJSON_CreateObject()))
		return NULL;

	for (int i = 0; i < SENSOR_COUNT; i++) {
		if (cfg->mqtt_temp_mask & (1 << i)) {
			snprintf(name, sizeof(name), "sensor%d", i + 1);
			cJSON_AddItemToObject(json, name, cJSON_CreateNumber(round_decimal(st->temp[i], 1)));
		}
	}

	for (int i = 0; i < VSENSOR_COUNT; i++) {
		if (cfg->mqtt_vtemp_mask & (1 << i)) {
			snprintf(name, sizeof(name), "vtemp%d", i + 1);
			cJSON_AddItemToObject(json, name, cJSON_CreateNumber(round_decimal(st->vtemp[i], 1)));
		}
		if (cfg->mqtt_vhumidity_mask & (1 << i)) {
			snprintf(name, sizeof(name), "vhumidity%d", i + 1);
			cJSON_AddItemToObject(json, name, cJSON_CreateNumber(round_decimal(st->vhumidity[i], 0)));
		}
		if (cfg->mqtt_vpressure_mask & (1 << i)) {
			snprintf(name, sizeof(name), "vpressure%d", i + 1);
			cJSON_AddItemToObject(json, name, cJSON_CreateNumber(round_decimal(st->vpressure[i], 0)));
		}
	}

	for (int i = 0; i < FAN_COUNT; i++) {
		if (cfg->mqtt_fan_rpm_mask & (1 << i)) {
			float rpm = st->fan_freq[i] * 60 / cfg->fans[i].rpm_factor;
			snprintf(name, sizeof(name), "fanrpm%d", i + 1);
			cJSON_AddItemToObject(json, name, cJSON_CreateNumber(round_decimal(rpm, 0)));
		}
		if (cfg->mqtt_fan_duty_mask & (1 << i)) {
			snprintf(name, sizeof(name), "fanpwm%d", i + 1);
			cJSON_AddItemToObject(json, name, cJSON_CreateNumber(round_decimal(st->fan_duty[i], 1)));
		}
	}

	for (int i = 0; i < MBFAN_COUNT; i++) {
		if (cfg->mqtt_mbfan_rpm_mask & (1 << i)) {
			float rpm = st->mbfan_freq[i] * 60 / cfg->mbfans[i].rpm_factor;
			snprintf(name, sizeof(name), "mbfanrpm%d", i + 1);
			cJSON_AddItemToObject(json, name, cJSON_CreateNumber(round_decimal(rpm, 0)));
		}
		if (cfg->mqtt_mbfan_duty_mask & (1 << i)) {
			snprintf(name, sizeof(name), "mbfanpwm%d", i + 1);
			cJSON_AddItemToObject(json, name, cJSON_CreateNumber(round_decimal(st->mbfan_duty[i], 1)));
		}
	}

	if (!(buf = cJSON_Print(json)))
		goto panic;
	cJSON_Delete(json);
	return buf;

panic:
	cJSON_Delete(json);
	return NULL;
}


static char* json_status_message()
{
	const struct fanpico_state *st = fanpico_state;
	char *buf;
	cJSON *json, *l, *o;
	int i;
	float rpm;
	time_t t;

	if (!(json = cJSON_CreateObject()))
		goto panic;

	cJSON_AddItemToObject(json, "name", cJSON_CreateString(cfg->name));
	cJSON_AddItemToObject(json, "hostname", cJSON_CreateString(net_state->hostname));
	if (network_ip())
		cJSON_AddItemToObject(json, "ip", cJSON_CreateString(network_ip()));

	/* fans */
	if (!(l = cJSON_CreateArray()))
		goto panic;
	cJSON_AddItemToObject(json, "fans", l);
	for (i = 0; i < FAN_COUNT; i++) {
		rpm = st->fan_freq[i] * 60 / cfg->fans[i].rpm_factor;
		if (!(o = cJSON_CreateObject()))
			goto panic;
		cJSON_AddItemToObject(o, "id", cJSON_CreateNumber(i + 1));
		cJSON_AddItemToObject(o, "rpm", cJSON_CreateNumber(rpm));
		cJSON_AddItemToObject(o, "pwm", cJSON_CreateNumber(st->fan_duty[i]));
		cJSON_AddItemToArray(l, o);
	}

	/* mbfans */
	if (!(l = cJSON_CreateArray()))
		goto panic;
	cJSON_AddItemToObject(json, "mbfans", l);
	for (i = 0; i < MBFAN_COUNT; i++) {
		rpm = st->mbfan_freq[i] * 60 / cfg->mbfans[i].rpm_factor;
		if (!(o = cJSON_CreateObject()))
			goto panic;
		cJSON_AddItemToObject(o, "id", cJSON_CreateNumber(i + 1));
		cJSON_AddItemToObject(o, "rpm", cJSON_CreateNumber(rpm));
		cJSON_AddItemToObject(o, "pwm", cJSON_CreateNumber(st->mbfan_duty[i]));
		cJSON_AddItemToArray(l, o);
	}

	/* sensors */
	if (!(l = cJSON_CreateArray()))
		goto panic;
	cJSON_AddItemToObject(json, "sensors", l);
	for (i = 0; i < SENSOR_COUNT; i++) {
		if (!(o = cJSON_CreateObject()))
			goto panic;
		cJSON_AddItemToObject(o, "id", cJSON_CreateNumber(i + 1));
		cJSON_AddItemToObject(o, "temp", cJSON_CreateNumber(round_decimal(st->temp[i], 1)));
		cJSON_AddItemToArray(l, o);
	}

	if ( rtc_get_time(&t) ) {
		/* Send Data Time stamp to broker for possible use */
		char datetime_buf[32];
		time_t_to_str(datetime_buf, sizeof(datetime_buf), t);
		cJSON_AddItemToObject(json, "datetime", cJSON_CreateString( datetime_buf ));
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

	if (!mqtt_client)
		return;

	log_msg(LOG_DEBUG, "fanpico_mqtt_publish(): start");

	if (strlen(cfg->mqtt_status_topic) > 0) {
		/* Generate status message */
		if (!(buf = json_status_message())) {
			log_msg(LOG_WARNING,"json_status_message(): failed");
			return;
		}
		mqtt_publish_message(cfg->mqtt_status_topic, buf, strlen(buf), mqtt_qos, 0,
				cfg->mqtt_status_topic);
		free(buf);
	}
	if (strlen(cfg->mqtt_ha_discovery_prefix) > 0) {
		char topic[64 + 10 + 1];

		if (mqtt_ha_discovery > 0)
			fanpico_mqtt_ha_discovery();
		/* Generate Home Assistant State message */
		if (!(buf = json_ha_state_message())) {
			log_msg(LOG_WARNING,"json_ha_state_message(): failed");
			return;
		}
		snprintf(topic, sizeof(topic), "%s/state", mqtt_ha_base_topic);
		mqtt_publish_message(topic, buf, strlen(buf), mqtt_qos, 0, topic);
		free(buf);
	}

	log_msg(LOG_DEBUG, "fanpico_mqtt_publish(): end");
}

void fanpico_mqtt_publish_temp()
{
	const struct fanpico_state *st = fanpico_state;
	char topic[MQTT_MAX_TOPIC_LEN + 8];
	char buf[64];

	if (!mqtt_client || strlen(cfg->mqtt_temp_topic) < 1)
		return;

	log_msg(LOG_DEBUG, "fanpico_mqtt_publish_temp(): start");
	for (int i = 0; i < SENSOR_COUNT; i++) {
		if (cfg->mqtt_temp_mask & (1 << i)) {
			snprintf(topic, sizeof(topic), cfg->mqtt_temp_topic, i + 1);
			snprintf(buf, sizeof(buf), "%.1f", st->temp[i]);
			mqtt_publish_message(topic, buf, strlen(buf), mqtt_qos, 0,
					cfg->mqtt_temp_topic);
		}
	}
	log_msg(LOG_DEBUG, "fanpico_mqtt_publish_temp(): end");
}

void fanpico_mqtt_publish_vsensor()
{
	const struct fanpico_state *st = fanpico_state;
	char topic[MQTT_MAX_TOPIC_LEN + 8];
	char buf[64];

	if (!mqtt_client)
		return;

	log_msg(LOG_DEBUG, "fanpico_mqtt_publish_vsensor(): start");
	for (int i = 0; i < VSENSOR_COUNT; i++) {
		if (strlen(cfg->mqtt_vtemp_topic) > 0 && cfg->mqtt_vtemp_mask & (1 << i)) {
			snprintf(topic, sizeof(topic), cfg->mqtt_vtemp_topic, i + 1);
			snprintf(buf, sizeof(buf), "%.1f", st->vtemp[i]);
			mqtt_publish_message(topic, buf, strlen(buf), mqtt_qos, 0,
					cfg->mqtt_vtemp_topic);
		}
		if (strlen(cfg->mqtt_vhumidity_topic) > 0 && cfg->mqtt_vhumidity_mask & (1 << i)) {
			snprintf(topic, sizeof(topic), cfg->mqtt_vhumidity_topic, i + 1);
			snprintf(buf, sizeof(buf), "%.0f", st->vhumidity[i]);
			mqtt_publish_message(topic, buf, strlen(buf), mqtt_qos, 0,
					cfg->mqtt_vhumidity_topic);
		}
		if (strlen(cfg->mqtt_vpressure_topic) > 0 && cfg->mqtt_vpressure_mask & (1 << i)) {
			snprintf(topic, sizeof(topic), cfg->mqtt_vpressure_topic, i + 1);
			snprintf(buf, sizeof(buf), "%.0f", st->vpressure[i]);
			mqtt_publish_message(topic, buf, strlen(buf), mqtt_qos, 0,
					cfg->mqtt_vpressure_topic);
		}
	}
	log_msg(LOG_DEBUG, "fanpico_mqtt_publish_vsensor(): end");
}

void fanpico_mqtt_publish_rpm()
{
	const struct fanpico_state *st = fanpico_state;
	char topic[MQTT_MAX_TOPIC_LEN + 8];
	char buf[64];

	if (!mqtt_client)
		return;

	log_msg(LOG_DEBUG, "fanpico_mqtt_publish_rpm(): start");
	if (strlen(cfg->mqtt_fan_rpm_topic) > 0) {
		for (int i = 0; i < FAN_COUNT; i++) {
			if (cfg->mqtt_fan_rpm_mask & (1 << i)) {
				float rpm = st->fan_freq[i] * 60 / cfg->fans[i].rpm_factor;
				snprintf(topic, sizeof(topic), cfg->mqtt_fan_rpm_topic, i + 1);
				snprintf(buf, sizeof(buf), "%.0f", rpm);
				mqtt_publish_message(topic, buf, strlen(buf), mqtt_qos, 0,
						cfg->mqtt_fan_rpm_topic);
			}
		}
	}
	if (strlen(cfg->mqtt_mbfan_rpm_topic) > 0) {
		for (int i = 0; i < MBFAN_COUNT; i++) {
			if (cfg->mqtt_mbfan_rpm_mask & (1 << i)) {
				float rpm = st->mbfan_freq[i] * 60 / cfg->mbfans[i].rpm_factor;
				snprintf(topic, sizeof(topic), cfg->mqtt_mbfan_rpm_topic, i + 1);
				snprintf(buf, sizeof(buf), "%.0f", rpm);
				mqtt_publish_message(topic, buf, strlen(buf), mqtt_qos, 0,
						cfg->mqtt_mbfan_rpm_topic);
			}
		}
	}
	log_msg(LOG_DEBUG, "fanpico_mqtt_publish_rpm(): end");
}

void fanpico_mqtt_publish_duty()
{
	const struct fanpico_state *st = fanpico_state;
	char topic[MQTT_MAX_TOPIC_LEN + 8];
	char buf[64];

	if (!mqtt_client)
		return;

	log_msg(LOG_DEBUG, "fanpico_mqtt_publish_duty(): start");
	if (strlen(cfg->mqtt_fan_duty_topic) > 0) {
		for (int i = 0; i < FAN_COUNT; i++) {
			if (cfg->mqtt_fan_duty_mask & (1 << i)) {
				snprintf(topic, sizeof(topic), cfg->mqtt_fan_duty_topic, i + 1);
				snprintf(buf, sizeof(buf), "%.1f", st->fan_duty[i]);
				mqtt_publish_message(topic, buf, strlen(buf), mqtt_qos, 0,
						cfg->mqtt_fan_duty_topic);
			}
		}
	}
	if (strlen(cfg->mqtt_mbfan_duty_topic) > 0) {
		for (int i = 0; i < MBFAN_COUNT; i++) {
			if (cfg->mqtt_mbfan_duty_mask & (1 << i)) {
				snprintf(topic, sizeof(topic), cfg->mqtt_mbfan_duty_topic, i + 1);
				snprintf(buf, sizeof(buf), "%.1f", st->mbfan_duty[i]);
				mqtt_publish_message(topic, buf, strlen(buf), mqtt_qos, 0,
						cfg->mqtt_mbfan_duty_topic);
			}
		}
	}
	log_msg(LOG_DEBUG, "fanpico_mqtt_publish_duty(): end");
}

void fanpico_mqtt_scpi_command()
{
	const struct fanpico_state *st = fanpico_state;
	char cmd[MQTT_CMD_MAX_LEN];
	int res;

	if (!mqtt_client || !mqtt_scpi_cmd_queued)
		return;

	log_msg(LOG_DEBUG, "MQTT process SCPI command: '%s'", mqtt_scpi_cmd);
	strncopy(cmd, mqtt_scpi_cmd, sizeof(cmd));
	process_command(st, (struct fanpico_config *)cfg, cmd);
	if ((res = last_command_status()) == 0) {
		log_msg(LOG_INFO, "MQTT SCPI command successful: '%s'", mqtt_scpi_cmd);
		send_mqtt_command_response(mqtt_scpi_cmd, res, "SCPI command successful");
	} else {
		log_msg(LOG_NOTICE, "MQTT SCPI command failed: '%s' (%d)", mqtt_scpi_cmd, res);
		if (res == -113)
			send_mqtt_command_response(mqtt_scpi_cmd, res, "SCPI unknown command");
		else
			send_mqtt_command_response(mqtt_scpi_cmd, res, "SCPI command failed");
	}

	mqtt_scpi_cmd[0] = 0;
	mqtt_scpi_cmd_queued = false;
}

#endif /* WIFI_SUPPORT */
