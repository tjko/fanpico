/* config.c
   Copyright (C) 2021-2025 Timo Kokkonen <tjko@iki.fi>

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
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "cJSON.h"
#include "pico_sensor_lib.h"
#ifdef WIFI_SUPPORT
#include "lwip/ip_addr.h"
#endif

#include "fanpico.h"

/* Default configuration embedded using  default_config.s */
extern const char fanpico_default_config[];


struct fanpico_config fanpico_config;
const struct fanpico_config *cfg = &fanpico_config;
auto_init_mutex(config_mutex_inst);
mutex_t *config_mutex = &config_mutex_inst;

int str2pwm_source(const char *s)
{
	int ret = PWM_FIXED;

	if (s) {
		if (!strncasecmp(s, "mbfan", 5))
			ret = PWM_MB;
		else if (!strncasecmp(s, "sensor", 6))
			ret = PWM_SENSOR;
		else if (!strncasecmp(s, "vsensor", 7))
			ret = PWM_VSENSOR;
		else if (!strncasecmp(s, "fan", 3))
			ret = PWM_FAN;
	}

	return ret;
}


const char* pwm_source2str(enum pwm_source_types source)
{
	if (source == PWM_MB)
		return "mbfan";
	else if (source == PWM_SENSOR)
		return "sensor";
	else if (source == PWM_VSENSOR)
		return "vsensor";
	else if (source == PWM_FAN)
		return "fan";

	return "fixed";
}


int valid_pwm_source_ref(enum pwm_source_types source, uint16_t s_id)
{
	int ret = 0;

	switch (source) {
	case PWM_FIXED:
		ret = (s_id >= 0 && s_id <= 100 ? 1 : 0);
		break;
		;;
	case PWM_MB:
		ret = (s_id >= 0 && s_id < MBFAN_MAX_COUNT ? 1 : 0);
		break;
	case PWM_SENSOR:
		ret = (s_id >= 0 && s_id < SENSOR_MAX_COUNT ? 1 : 0);
		break;
	case PWM_VSENSOR:
		ret = (s_id >= 0 && s_id < VSENSOR_MAX_COUNT ? 1 : 0);
		break;
	case PWM_FAN:
		ret = (s_id >= 0 && s_id < FAN_MAX_COUNT ? 1 : 0);
		break;
	}

	return ret;
}

int str2vsmode(const char *s)
{
	int ret = VSMODE_MANUAL;

	if (s) {
		if (!strncasecmp(s, "max", 3))
			ret = VSMODE_MAX;
		else if (!strncasecmp(s, "min", 3))
			ret = VSMODE_MIN;
		else if (!strncasecmp(s, "avg", 3))
			ret = VSMODE_AVG;
		else if (!strncasecmp(s, "delta", 5))
			ret = VSMODE_DELTA;
		else if (!strncasecmp(s, "onewire", 7))
			ret = VSMODE_ONEWIRE;
		else if (!strncasecmp(s, "i2c", 3))
			ret = VSMODE_I2C;
	}

	return ret;
}

const char* vsmode2str(enum vsensor_modes mode)
{
	if (mode == VSMODE_MAX)
		return "max";
	else if (mode == VSMODE_MIN)
		return "min";
	else if (mode == VSMODE_AVG)
		return "avg";
	else if (mode == VSMODE_DELTA)
		return "delta";
	else if (mode == VSMODE_ONEWIRE)
		return "onewire";
	else if (mode == VSMODE_I2C)
		return "i2c";

	return "manual";
}

int str2rpm_mode(const char *s)
{
	int ret = RMODE_TACHO;

	if (s) {
		if (!strncasecmp(s, "lra", 5))
			ret = RMODE_LRA;
	}

	return ret;
}

const char* rpm_mode2str(enum rpm_modes mode)
{
	if (mode == RMODE_LRA)
		return "lra";

	return "tacho";
}

int str2tacho_source(const char *s)
{
	int ret = 0;

	if (s) {
		if (!strncasecmp(s, "fixed", 5))
			ret = TACHO_FIXED;
		else if (!strncasecmp(s, "fan", 3))
			ret = TACHO_FAN;
		else if (!strncasecmp(s, "min", 3))
			ret = TACHO_MIN;
		else if (!strncasecmp(s, "max", 3))
			ret = TACHO_MAX;
		else if (!strncasecmp(s, "avg", 3))
			ret = TACHO_AVG;
	}

	return ret;
}


const char* tacho_source2str(enum tacho_source_types source)
{
	if (source == TACHO_FAN)
		return "fan";
	else if (source == TACHO_MIN)
		return "min";
	else if (source == TACHO_MAX)
		return "max";
	else if (source == TACHO_AVG)
		return "avg";

	return "fixed";
}


const char* onewireaddr2str(uint64_t addr)
{
	static char s[17];

	snprintf(s, sizeof(s), "%016llx", addr);
	return s;
}


uint64_t str2onewireaddr(const char *s)
{
	return strtoull(s, NULL, 16);
}


int valid_tacho_source_ref(enum tacho_source_types source, uint16_t s_id)
{
	int ret = 0;

	switch (source) {
	case TACHO_FIXED:
		ret = (s_id >= 0 && s_id <= 50000 ? 1 : 0);
		break;
		;;
	case TACHO_FAN:
	case TACHO_MIN:
	case TACHO_MAX:
	case TACHO_AVG:
		ret = (s_id >= 0 && s_id < FAN_MAX_COUNT ? 1 : 0);
		break;
	}

	return ret;
}


static void json2pwm_map(cJSON *item, struct pwm_map *map)
{
	cJSON *row;
	int c = 0;

	cJSON_ArrayForEach(row, item) {
		if (c < MAX_MAP_POINTS) {
			map->pwm[c][0] = cJSON_GetNumberValue(cJSON_GetArrayItem(row, 0));
			map->pwm[c][1] = cJSON_GetNumberValue(cJSON_GetArrayItem(row, 1));
			c++;
		}
	}

	map->points = c;
}


static cJSON* pwm_map2json(const struct pwm_map *map)
{
	int i;
	cJSON *o, *row;

	if ((o = cJSON_CreateArray()) == NULL)
		return NULL;

	for (i = 0; i < map->points; i++) {
		if ((row = cJSON_CreateArray())) {
			cJSON_AddItemToArray(row, cJSON_CreateNumber(map->pwm[i][0]));
			cJSON_AddItemToArray(row, cJSON_CreateNumber(map->pwm[i][1]));
			cJSON_AddItemToArray(o, row);
		}
	}
	return o;
}


static void json2filter(cJSON *item, enum signal_filter_types *filter, void **filter_ctx)
{
	cJSON *args;

	*filter = str2filter(cJSON_GetStringValue(cJSON_GetObjectItem(item, "name")));
	if ((args = cJSON_GetObjectItem(item, "args")) && *filter != FILTER_NONE) {
		*filter_ctx =  filter_parse_args(*filter, cJSON_GetStringValue(args));
		if (!*filter_ctx)
			*filter = FILTER_NONE;
	}
}


static cJSON* filter2json(enum signal_filter_types filter, void *filter_ctx)
{
	cJSON *o;
	char *s;

	if ((o = cJSON_CreateObject()) == NULL)
		return NULL;

	cJSON_AddItemToObject(o, "name", cJSON_CreateString(filter2str(filter)));
	if ((s = filter_print_args(filter, filter_ctx))) {
		cJSON_AddItemToObject(o, "args", cJSON_CreateString(s));
		free(s);
	}

	return o;
}


static void json2tacho_map(cJSON *item, struct tacho_map *map)
{
	cJSON *row;
	int c = 0;

	cJSON_ArrayForEach(row, item) {
		if (c < MAX_MAP_POINTS) {
			map->tacho[c][0] = cJSON_GetNumberValue(cJSON_GetArrayItem(row, 0));
			map->tacho[c][1] = cJSON_GetNumberValue(cJSON_GetArrayItem(row, 1));
			c++;
		}
	}

	map->points = c;
}

static cJSON* tacho_map2json(const struct tacho_map *map)
{
	int i;
	cJSON *o, *row;

	if ((o = cJSON_CreateArray()) == NULL)
		return NULL;

	for (i = 0; i < map->points; i++) {
		if ((row = cJSON_CreateArray())) {
			cJSON_AddItemToArray(row, cJSON_CreateNumber(map->tacho[i][0]));
			cJSON_AddItemToArray(row, cJSON_CreateNumber(map->tacho[i][1]));
			cJSON_AddItemToArray(o, row);
		}
	}
	return o;
}

static void json2tacho_sources(cJSON *item, uint8_t *sources)
{
	int i;
	cJSON *o;

	for (i = 0; i < FAN_MAX_COUNT; i++)
		sources[i] = 0;

	cJSON_ArrayForEach(o, item) {
		i = cJSON_GetNumberValue(o) - 1;
		if (i >= 0 && i < FAN_MAX_COUNT)
			sources[i] = 1;
	}
}

static cJSON* tacho_sources2json(const uint8_t *sources)
{
	int i;
	cJSON *o;

	if ((o = cJSON_CreateArray()) == NULL)
		return NULL;

	for (i = 0; i < FAN_COUNT; i++) {
		if (sources[i])
			cJSON_AddItemToArray(o, cJSON_CreateNumber(i + 1));
	}

	return o;
}

static void json2temp_map(cJSON *item, struct temp_map *map)
{
	cJSON *row;
	int c = 0;

	cJSON_ArrayForEach(row, item) {
		if (c < MAX_MAP_POINTS) {
			map->temp[c][0] = cJSON_GetNumberValue(cJSON_GetArrayItem(row, 0));
			map->temp[c][1] = cJSON_GetNumberValue(cJSON_GetArrayItem(row, 1));
			c++;
		}
	}

	map->points = c;
}


static cJSON* temp_map2json(const struct temp_map *map)
{
	int i;
	cJSON *o, *row;

	if ((o = cJSON_CreateArray()) == NULL)
		return NULL;

	for (i = 0; i < map->points; i++) {
		if ((row = cJSON_CreateArray())) {
			cJSON_AddItemToArray(row, cJSON_CreateNumber(map->temp[i][0]));
			cJSON_AddItemToArray(row, cJSON_CreateNumber(map->temp[i][1]));
			cJSON_AddItemToArray(o, row);
		}
	}
	return o;
}


static void json2vsensors(cJSON *item, uint8_t *s)
{
	cJSON *o;
	int i,val;
	int count = 0;

	for (i = 0; i < SENSOR_MAX_COUNT; i++)
		s[i] = 0;

	cJSON_ArrayForEach(o, item) {
		val = cJSON_GetNumberValue(o);
		if (count < SENSOR_COUNT && val >= 1 && val <= SENSOR_COUNT) {
			s[count++] = val;
		}
	}
}


static cJSON* vsensors2json(const uint8_t *s)
{
	int i;
	cJSON *o;

	if ((o = cJSON_CreateArray()) == NULL)
		return NULL;

	for (i = 0; i < SENSOR_COUNT; i++) {
		if (s[i]) {
			cJSON_AddItemToArray(o, cJSON_CreateNumber(s[i]));
		}
	}
	return o;
}

#ifdef WIFI_SUPPORT
static void json2iplist(cJSON *item, ip_addr_t *list, uint32_t len)
{
	cJSON *o;
	char *val;
	ip_addr_t tmpip;
	int count = 0;

	for (int i = 0; i < len; i++)
		ip_addr_set_any(0, &list[i]);

	cJSON_ArrayForEach(o, item) {
		val = cJSON_GetStringValue(o);
		if (val && count < len) {
			if (ipaddr_aton(val, &tmpip)) {
				ip_addr_copy(list[count++], tmpip);
			}
		}
	}
}

static cJSON* iplist2json(const ip_addr_t *list, uint32_t len)
{
	cJSON *o;

	if ((o = cJSON_CreateArray()) == NULL)
		return NULL;

	for (int i = 0; i < len; i++) {
		cJSON_AddItemToArray(o, cJSON_CreateString(ipaddr_ntoa(&list[i])));
	}

	return o;
}

static void json2acllist(cJSON *item, acl_entry_t *list, uint32_t len)
{
	cJSON *o;
	char *val;
	ip_addr_t tmpip;
	int count = 0;
	char *s, *tok, *saveptr;
	int prefix;

	for (int i = 0; i < len; i++) {
		ip_addr_set_any(0, &list[i].ip);
		list[i].prefix = 0;
	}

	cJSON_ArrayForEach(o, item) {
		val = cJSON_GetStringValue(o);
		if (val && count < len && (s = strdup(val))) {
			tok = strtok_r(s, "/", &saveptr);
			if (tok && ipaddr_aton(tok, &tmpip)) {
				tok = strtok_r(NULL, "/", &saveptr);
				if (tok && str_to_int(tok, &prefix, 10)) {
					ip_addr_copy(list[count].ip, tmpip);
					list[count].prefix = clamp_int(prefix, 0,
								IP_IS_V6(tmpip) ? 128 : 32);
					count++;
				}
			}
			free(s);
		}
	}

}

static cJSON* acllist2json(const acl_entry_t *list, uint32_t len)
{
	cJSON *o;
	char tmp[128];
	int count = 0;

	if ((o = cJSON_CreateArray()) == NULL)
		return NULL;

	for (int i = 0; i < len; i++) {
		if (list[i].prefix > 0) {
			snprintf(tmp, sizeof(tmp), "%s/%u", ipaddr_ntoa(&list[i].ip),
				list[i].prefix);
			cJSON_AddItemToArray(o, cJSON_CreateString(tmp));
			count++;
		}
	}

	if (count < 1) {
		cJSON_Delete(o);
		o = NULL;
	}

	return o;
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


static void json2sshpubkeys(cJSON *list, struct ssh_public_key *keys)
{
	cJSON *r, *user, *pkey;
	struct ssh_public_key *k;
	int idx = 0;

	for (int i = 0; i < SSH_MAX_PUB_KEYS; i++) {
		keys[i].username[0] = 0;
		keys[i].type[0] = 0;
		keys[i].name[0] = 0;
		keys[i].pubkey_size = 0;
	}

	cJSON_ArrayForEach(r, list) {
		k = &keys[idx];
		user = cJSON_GetObjectItem(r, "user");
		pkey = cJSON_GetObjectItem(r, "pubkey");
		if (user && pkey) {
			if (str_to_ssh_pubkey(cJSON_GetStringValue(pkey), k) == 0) {
				strncopy(k->username, cJSON_GetStringValue(user),
					sizeof(k->username));
				idx++;
			}
		}
		if (idx >= SSH_MAX_PUB_KEYS)
			break;
	}
}

static cJSON* sshpubkeys2json(const struct ssh_public_key *keys)
{
	const size_t buf_len = 256;
	char *buf;
	int count = 0;
	cJSON *o, *r;

	if (!(buf = calloc(1, buf_len)))
		return NULL;

	if ((o = cJSON_CreateArray())) {
		for (int i = 0; i < SSH_MAX_PUB_KEYS; i++) {
			const struct ssh_public_key *k = &keys[i];
			if (k->pubkey_size > 0 && strlen(k->username) > 0) {
				if (ssh_pubkey_to_str(k, buf, buf_len)) {
					if ((r = cJSON_CreateObject())) {
						cJSON_AddItemToObject(r, "pubkey",
								cJSON_CreateString(buf));
						cJSON_AddItemToObject(r, "user",
								cJSON_CreateString(k->username));
						cJSON_AddItemToArray(o, r);
						count++;
					}
				}
			}
		}
	}

	free(buf);
	if (count < 1) {
		cJSON_Delete(o);
		o = NULL;
	}

	return o;
}
#endif

void clear_config(struct fanpico_config *cfg)
{
	int i, j;
	struct sensor_input *s;
	struct vsensor_input *vs;
	struct fan_output *f;
	struct mb_input *m;

	memset(cfg, 0, sizeof(struct fanpico_config));

	for (i = 0; i < SENSOR_MAX_COUNT; i++) {
		s = &cfg->sensors[i];

		s->name[0] = 0;
		s->type = TEMP_INTERNAL;
		s->thermistor_nominal = 0.0;
		s->beta_coefficient = 0.0;
		s->temp_nominal = 0.0;
		s->temp_offset = 0.0;
		s->temp_coefficient = 0.0;
		s->map.points = 0;
		s->filter = FILTER_NONE;
		s->filter_ctx = NULL;
	}

	for (i = 0; i < VSENSOR_MAX_COUNT; i++) {
		vs = &cfg->vsensors[i];

		vs->name[0] = 0;
		vs->mode = VSMODE_MANUAL;
		vs->default_temp = 0.0;
		vs->timeout = 60;
		for (j = 0; j < VSENSOR_SOURCE_MAX_COUNT; j++)
			vs->sensors[j] = 0;
		vs->onewire_addr = 0;
		vs->i2c_type = 0;
		vs->i2c_addr = 0;
		vs->map.points = 2;
		vs->map.temp[0][0] = 20.0;
		vs->map.temp[0][1] = 0.0;
		vs->map.temp[1][0] = 50.0;
		vs->map.temp[1][1] = 100.0;
		vs->filter = FILTER_NONE;
		vs->filter_ctx = NULL;

		cfg->vtemp[i] = 0.0;
		cfg->vhumidity[i] = 0.0;
		cfg->vpressure[i] = 0.0;
		cfg->vtemp_updated[i] = from_us_since_boot(0);
		cfg->i2c_context[i] = NULL;
	}

	for (i = 0; i < FAN_MAX_COUNT; i++) {
		f = &cfg->fans[i];

		f->name[0] = 0;
		f->min_pwm = 0;
		f->max_pwm = 0;
		f->pwm_coefficient = 0.0;
		f->s_type = PWM_FIXED;
		f->s_id = 0;
		f->map.points = 0;
		f->rpm_mode = RMODE_TACHO;
		f->lra_low = 1000;
		f->lra_high = 0;
		f->rpm_factor = 2;
		f->filter = FILTER_NONE;
		f->filter_ctx = NULL;
		f->tacho_hyst = FAN_TACHO_HYSTERESIS;
		f->pwm_hyst = FAN_PWM_HYSTERESIS;
	}

	for (i = 0; i < MBFAN_MAX_COUNT; i++) {
		m=&cfg->mbfans[i];

		m->name[0] = 0;
		m->min_rpm = 0;
		m->max_rpm = 0;
		m->rpm_mode = RMODE_TACHO;
		m->lra_treshold = 200;
		m->lra_invert = false;
		m->rpm_coefficient = 0.0;
		m->rpm_factor = 2;
		m->s_type = TACHO_FIXED;
		m->s_id = 0;
		m->map.points = 0;
		m->filter = FILTER_NONE;
		m->filter_ctx = NULL;
		for (j = 0; j < FAN_MAX_COUNT; j++)
			m->sources[j] = 0;
	}

	cfg->local_echo = false;
	cfg->spi_active = false;
	cfg->serial_active = false;
	cfg->onewire_active = false;
	cfg->i2c_speed = I2C_DEFAULT_SPEED;
	cfg->adc_vref = ADC_REF_VOLTAGE;
	cfg->led_mode = 0;
	strncopy(cfg->name, "fanpico1", sizeof(cfg->name));
	strncopy(cfg->display_type, "default", sizeof(cfg->display_type));
	strncopy(cfg->display_theme, "default", sizeof(cfg->display_theme));
	strncopy(cfg->display_logo, "default", sizeof(cfg->display_logo));
	strncopy(cfg->display_layout_r, "", sizeof(cfg->display_layout_r));
	strncopy(cfg->timezone, "", sizeof(cfg->timezone));
#ifdef WIFI_SUPPORT
	cfg->wifi_ssid[0] = 0;
	cfg->wifi_passwd[0] = 0;
	strncopy(cfg->wifi_auth_mode, "default", sizeof(cfg->wifi_auth_mode));
	cfg->wifi_mode = 0;
	cfg->hostname[0] = 0;
	strncopy(cfg->wifi_country, "XX", sizeof(cfg->wifi_country));
	for (int i = 0; i < DNS_MAX_SERVERS; i++) {
		ip_addr_set_any(0, &cfg->dns_servers[i]);
	}
	ip_addr_set_any(0, &cfg->syslog_server);
	ip_addr_set_any(0, &cfg->ntp_server);
	ip_addr_set_any(0, &cfg->ip);
	ip_addr_set_any(0, &cfg->netmask);
	ip_addr_set_any(0, &cfg->gateway);
	cfg->mqtt_server[0] = 0;
	cfg->mqtt_port = 0;
	cfg->mqtt_tls = true;
	cfg->mqtt_allow_scpi = false;
	cfg->mqtt_user[0] = 0;
	cfg->mqtt_pass[0] = 0;
	cfg->mqtt_status_topic[0] = 0;
	cfg->mqtt_cmd_topic[0] = 0;
	cfg->mqtt_resp_topic[0] = 0;
	cfg->mqtt_temp_mask = 0;
	cfg->mqtt_vtemp_mask = 0;
	cfg->mqtt_vhumidity_mask = 0;
	cfg->mqtt_vpressure_mask = 0;
	cfg->mqtt_fan_rpm_mask = 0;
	cfg->mqtt_fan_duty_mask = 0;
	cfg->mqtt_mbfan_rpm_mask = 0;
	cfg->mqtt_mbfan_duty_mask = 0;
	cfg->mqtt_temp_topic[0] = 0;
	cfg->mqtt_vtemp_topic[0] = 0;
	cfg->mqtt_vhumidity_topic[0] = 0;
	cfg->mqtt_vpressure_topic[0] = 0;
	cfg->mqtt_fan_rpm_topic[0] = 0;
	cfg->mqtt_fan_duty_topic[0] = 0;
	cfg->mqtt_mbfan_rpm_topic[0] = 0;
	cfg->mqtt_mbfan_duty_topic[0] = 0;
	cfg->mqtt_status_interval = DEFAULT_MQTT_STATUS_INTERVAL;
	cfg->mqtt_temp_interval = DEFAULT_MQTT_TEMP_INTERVAL;
	cfg->mqtt_vsensor_interval = DEFAULT_MQTT_TEMP_INTERVAL;
	cfg->mqtt_rpm_interval = DEFAULT_MQTT_RPM_INTERVAL;
	cfg->mqtt_duty_interval = DEFAULT_MQTT_DUTY_INTERVAL;
	cfg->mqtt_ha_discovery_prefix[0] = 0;
	cfg->telnet_active = false;
	cfg->telnet_auth = true;
	cfg->telnet_raw_mode = false;
	cfg->telnet_port = 0;
	cfg->telnet_user[0] = 0;
	cfg->telnet_pwhash[0] = 0;
	cfg->snmp_active = 0;
	strncopy(cfg->snmp_community, "public", sizeof(cfg->snmp_community));
	cfg->snmp_community_write[0] = 0;
	cfg->snmp_contact[0] = 0;
	cfg->snmp_location[0] = 0;
	cfg->snmp_community_trap[0] = 0;
	cfg->snmp_auth_traps = false;
	ip_addr_set_any(0, &cfg->snmp_trap_dst);
	cfg->ssh_active = false;
	cfg->ssh_auth = true;
	cfg->ssh_port = 0;
	cfg->ssh_user[0] = 0;
	cfg->ssh_pwhash[0] = 0;
	for (int i = 0; i < SSH_MAX_PUB_KEYS; i++) {
		cfg->ssh_pub_keys[i].username[0] = 0;
		cfg->ssh_pub_keys[i].type[0] = 0;
		cfg->ssh_pub_keys[i].name[0] = 0;
		cfg->ssh_pub_keys[i].pubkey_size = 0;
	}
#endif

}


cJSON *config_to_json(const struct fanpico_config *cfg)
{
	cJSON *config = cJSON_CreateObject();
	cJSON *fans, *mbfans, *sensors, *vsensors, *o;
	int i;

	if (!config)
		return NULL;

	cJSON_AddItemToObject(config, "id", cJSON_CreateString("fanpico-config-v1"));
	cJSON_AddItemToObject(config, "debug", cJSON_CreateNumber(get_debug_level()));
	cJSON_AddItemToObject(config, "log_level", cJSON_CreateNumber(get_log_level()));
	cJSON_AddItemToObject(config, "syslog_level", cJSON_CreateNumber(get_syslog_level()));
	cJSON_AddItemToObject(config, "local_echo", cJSON_CreateBool(cfg->local_echo));
	cJSON_AddItemToObject(config, "led_mode", cJSON_CreateNumber(cfg->led_mode));
	cJSON_AddItemToObject(config, "spi_active", cJSON_CreateNumber(cfg->spi_active));
	cJSON_AddItemToObject(config, "serial_active", cJSON_CreateNumber(cfg->serial_active));
	cJSON_AddItemToObject(config, "onewire_active", cJSON_CreateNumber(cfg->onewire_active));
	cJSON_AddItemToObject(config, "i2c_speed", cJSON_CreateNumber(cfg->i2c_speed));
	cJSON_AddItemToObject(config, "adc_vref", cJSON_CreateNumber(cfg->adc_vref)); //Zitt
	if (strlen(cfg->display_type) > 0)
		cJSON_AddItemToObject(config, "display_type", cJSON_CreateString(cfg->display_type));
	if (strlen(cfg->display_theme) > 0)
		cJSON_AddItemToObject(config, "display_theme", cJSON_CreateString(cfg->display_theme));
	if (strlen(cfg->display_logo) > 0)
		cJSON_AddItemToObject(config, "display_logo", cJSON_CreateString(cfg->display_logo));
	if (strlen(cfg->display_layout_r) > 0)
		cJSON_AddItemToObject(config, "display_layout_r", cJSON_CreateString(cfg->display_layout_r));
	if (strlen(cfg->name) > 0)
		cJSON_AddItemToObject(config, "name", cJSON_CreateString(cfg->name));
	if (strlen(cfg->timezone) > 0)
		cJSON_AddItemToObject(config, "timezone", cJSON_CreateString(cfg->timezone));

#ifdef WIFI_SUPPORT
	if (strlen(cfg->hostname) > 0)
		cJSON_AddItemToObject(config, "hostname", cJSON_CreateString(cfg->hostname));
	if (strlen(cfg->wifi_country) > 0)
		cJSON_AddItemToObject(config, "wifi_country", cJSON_CreateString(cfg->wifi_country));
	if (strlen(cfg->wifi_ssid) > 0)
		cJSON_AddItemToObject(config, "wifi_ssid", cJSON_CreateString(cfg->wifi_ssid));
	if (strlen(cfg->wifi_passwd) > 0) {
		char *p = base64encode(cfg->wifi_passwd);
		if (p) {
			cJSON_AddItemToObject(config, "wifi_passwd", cJSON_CreateString(p));
			free(p);
		}
	}
	if (strlen(cfg->wifi_auth_mode) > 0) {
		cJSON_AddItemToObject(config, "wifi_auth_mode", cJSON_CreateString(cfg->wifi_auth_mode));
	}
	if (cfg->wifi_mode != 0) {
		cJSON_AddItemToObject(config, "wifi_mode", cJSON_CreateNumber(cfg->wifi_mode));
	}
	if (!ip_addr_isany(&cfg->dns_servers[0]))
		cJSON_AddItemToObject(config, "dns_servers", iplist2json(cfg->dns_servers, DNS_MAX_SERVERS));
	if (!ip_addr_isany(&cfg->syslog_server))
		cJSON_AddItemToObject(config, "syslog_server", cJSON_CreateString(ipaddr_ntoa(&cfg->syslog_server)));
	if (!ip_addr_isany(&cfg->ntp_server))
		cJSON_AddItemToObject(config, "ntp_server", cJSON_CreateString(ipaddr_ntoa(&cfg->ntp_server)));
	if (!ip_addr_isany(&cfg->ip))
		cJSON_AddItemToObject(config, "ip", cJSON_CreateString(ipaddr_ntoa(&cfg->ip)));
	if (!ip_addr_isany(&cfg->netmask))
		cJSON_AddItemToObject(config, "netmask", cJSON_CreateString(ipaddr_ntoa(&cfg->netmask)));
	if (!ip_addr_isany(&cfg->gateway))
		cJSON_AddItemToObject(config, "gateway", cJSON_CreateString(ipaddr_ntoa(&cfg->gateway)));
	if (strlen(cfg->mqtt_server) > 0)
		cJSON_AddItemToObject(config, "mqtt_server", cJSON_CreateString(cfg->mqtt_server));
	if (cfg->mqtt_port > 0)
		cJSON_AddItemToObject(config, "mqtt_port", cJSON_CreateNumber(cfg->mqtt_port));
	if (strlen(cfg->mqtt_user) > 0)
		cJSON_AddItemToObject(config, "mqtt_user", cJSON_CreateString(cfg->mqtt_user));
	if (strlen(cfg->mqtt_pass) > 0) {
		char *p = base64encode(cfg->mqtt_pass);
		if (p) {
			cJSON_AddItemToObject(config, "mqtt_pass", cJSON_CreateString(p));
			free(p);
		}
	}
	if (strlen(cfg->mqtt_status_topic) > 0)
		cJSON_AddItemToObject(config, "mqtt_status_topic",
				cJSON_CreateString(cfg->mqtt_status_topic));
	if (strlen(cfg->mqtt_cmd_topic) > 0)
		cJSON_AddItemToObject(config, "mqtt_cmd_topic",
				cJSON_CreateString(cfg->mqtt_cmd_topic));
	if (strlen(cfg->mqtt_resp_topic) > 0)
		cJSON_AddItemToObject(config, "mqtt_resp_topic",
				cJSON_CreateString(cfg->mqtt_resp_topic));
	if (cfg->mqtt_tls != true)
		cJSON_AddItemToObject(config, "mqtt_tls", cJSON_CreateNumber(cfg->mqtt_tls));
	if (cfg->mqtt_allow_scpi == true)
		cJSON_AddItemToObject(config, "mqtt_allow_scpi",
				cJSON_CreateNumber(cfg->mqtt_allow_scpi));
	if (cfg->mqtt_status_interval != DEFAULT_MQTT_STATUS_INTERVAL)
		cJSON_AddItemToObject(config, "mqtt_status_interval",
				cJSON_CreateNumber(cfg->mqtt_status_interval));
	if (cfg->mqtt_temp_interval != DEFAULT_MQTT_TEMP_INTERVAL)
		cJSON_AddItemToObject(config, "mqtt_temp_interval",
				cJSON_CreateNumber(cfg->mqtt_temp_interval));
	if (cfg->mqtt_vsensor_interval != DEFAULT_MQTT_TEMP_INTERVAL)
		cJSON_AddItemToObject(config, "mqtt_vsensor_interval",
				cJSON_CreateNumber(cfg->mqtt_vsensor_interval));
	if (cfg->mqtt_rpm_interval != DEFAULT_MQTT_RPM_INTERVAL)
		cJSON_AddItemToObject(config, "mqtt_rpm_interval",
				cJSON_CreateNumber(cfg->mqtt_rpm_interval));
	if (cfg->mqtt_duty_interval != DEFAULT_MQTT_DUTY_INTERVAL)
		cJSON_AddItemToObject(config, "mqtt_duty_interval",
				cJSON_CreateNumber(cfg->mqtt_duty_interval));
	if (cfg->mqtt_temp_mask)
		cJSON_AddItemToObject(config, "mqtt_temp_mask",
				cJSON_CreateString(
					bitmask_to_str(cfg->mqtt_temp_mask, SENSOR_MAX_COUNT,
						1, true)));
	if (cfg->mqtt_vtemp_mask)
		cJSON_AddItemToObject(config, "mqtt_vtemp_mask",
				cJSON_CreateString(
					bitmask_to_str(cfg->mqtt_vtemp_mask, VSENSOR_MAX_COUNT,
						1, true)));
	if (cfg->mqtt_vhumidity_mask)
		cJSON_AddItemToObject(config, "mqtt_vhumidity_mask",
				cJSON_CreateString(
					bitmask_to_str(cfg->mqtt_vhumidity_mask, VSENSOR_MAX_COUNT,
						1, true)));
	if (cfg->mqtt_vpressure_mask)
		cJSON_AddItemToObject(config, "mqtt_vpressure_mask",
				cJSON_CreateString(
					bitmask_to_str(cfg->mqtt_vpressure_mask, VSENSOR_MAX_COUNT,
						1, true)));
	if (cfg->mqtt_fan_rpm_mask)
		cJSON_AddItemToObject(config, "mqtt_fan_rpm_mask",
				cJSON_CreateString(
					bitmask_to_str(cfg->mqtt_fan_rpm_mask, FAN_MAX_COUNT,
						1, true)));
	if (cfg->mqtt_fan_duty_mask)
		cJSON_AddItemToObject(config, "mqtt_fan_duty_mask",
				cJSON_CreateString(
					bitmask_to_str(cfg->mqtt_fan_duty_mask, FAN_MAX_COUNT,
						1, true)));
	if (cfg->mqtt_mbfan_rpm_mask)
		cJSON_AddItemToObject(config, "mqtt_mbfan_rpm_mask",
				cJSON_CreateString(
					bitmask_to_str(cfg->mqtt_mbfan_rpm_mask, MBFAN_MAX_COUNT,
						1, true)));
	if (cfg->mqtt_mbfan_duty_mask)
		cJSON_AddItemToObject(config, "mqtt_mbfan_duty_mask",
				cJSON_CreateString(
					bitmask_to_str(cfg->mqtt_mbfan_duty_mask, MBFAN_MAX_COUNT,
						1, true)));
	if (strlen(cfg->mqtt_temp_topic) > 0)
		cJSON_AddItemToObject(config, "mqtt_temp_topic",
				cJSON_CreateString(cfg->mqtt_temp_topic));
	if (strlen(cfg->mqtt_vtemp_topic) > 0)
		cJSON_AddItemToObject(config, "mqtt_vtemp_topic",
				cJSON_CreateString(cfg->mqtt_vtemp_topic));
	if (strlen(cfg->mqtt_vhumidity_topic) > 0)
		cJSON_AddItemToObject(config, "mqtt_vhumidity_topic",
				cJSON_CreateString(cfg->mqtt_vhumidity_topic));
	if (strlen(cfg->mqtt_vpressure_topic) > 0)
		cJSON_AddItemToObject(config, "mqtt_vpressure_topic",
				cJSON_CreateString(cfg->mqtt_vpressure_topic));
	if (strlen(cfg->mqtt_fan_rpm_topic) > 0)
		cJSON_AddItemToObject(config, "mqtt_fan_rpm_topic",
				cJSON_CreateString(cfg->mqtt_fan_rpm_topic));
	if (strlen(cfg->mqtt_fan_duty_topic) > 0)
		cJSON_AddItemToObject(config, "mqtt_fan_duty_topic",
				cJSON_CreateString(cfg->mqtt_fan_duty_topic));
	if (strlen(cfg->mqtt_mbfan_rpm_topic) > 0)
		cJSON_AddItemToObject(config, "mqtt_mbfan_rpm_topic",
				cJSON_CreateString(cfg->mqtt_mbfan_rpm_topic));
	if (strlen(cfg->mqtt_mbfan_duty_topic) > 0)
		cJSON_AddItemToObject(config, "mqtt_mbfan_duty_topic",
				cJSON_CreateString(cfg->mqtt_mbfan_duty_topic));
	if (strlen(cfg->mqtt_ha_discovery_prefix) > 0)
		cJSON_AddItemToObject(config, "mqtt_ha_discovery_prefix",
				cJSON_CreateString(cfg->mqtt_ha_discovery_prefix));
	if (cfg->telnet_active)
		cJSON_AddItemToObject(config, "telnet_active", cJSON_CreateNumber(cfg->telnet_active));
	if (cfg->telnet_auth != true)
		cJSON_AddItemToObject(config, "telnet_auth", cJSON_CreateNumber(cfg->telnet_auth));
	if (cfg->telnet_raw_mode)
		cJSON_AddItemToObject(config, "telnet_raw_mode", cJSON_CreateNumber(cfg->telnet_raw_mode));
	if (cfg->telnet_port > 0)
		cJSON_AddItemToObject(config, "telnet_port", cJSON_CreateNumber(cfg->telnet_port));
	if (strlen(cfg->telnet_user) > 0)
		cJSON_AddItemToObject(config, "telnet_user", cJSON_CreateString(cfg->telnet_user));
	if (strlen(cfg->telnet_pwhash) > 0)
		cJSON_AddItemToObject(config, "telnet_pwhash", cJSON_CreateString(cfg->telnet_pwhash));
	if ((o = acllist2json(cfg->telnet_acls, TELNET_MAX_ACL_ENTRIES))) {
		cJSON_AddItemToObject(config, "telnet_acls", o);
	}
	if (cfg->snmp_active)
		cJSON_AddItemToObject(config, "snmp_active", cJSON_CreateNumber(cfg->snmp_active));
	if (strlen(cfg->snmp_community) > 0)
		cJSON_AddItemToObject(config, "snmp_community", cJSON_CreateString(cfg->snmp_community));
	if (strlen(cfg->snmp_community_write) > 0)
		cJSON_AddItemToObject(config, "snmp_community_write", cJSON_CreateString(cfg->snmp_community_write));
	if (strlen(cfg->snmp_contact) > 0)
		cJSON_AddItemToObject(config, "snmp_contact", cJSON_CreateString(cfg->snmp_contact));
	if (strlen(cfg->snmp_location) > 0)
		cJSON_AddItemToObject(config, "snmp_location", cJSON_CreateString(cfg->snmp_location));
	if (strlen(cfg->snmp_community_trap) > 0)
		cJSON_AddItemToObject(config, "snmp_community_trap", cJSON_CreateString(cfg->snmp_community_trap));
	if (cfg->snmp_auth_traps)
		cJSON_AddItemToObject(config, "snmp_auth_traps", cJSON_CreateNumber(cfg->snmp_auth_traps));
	if (!ip_addr_isany(&cfg->snmp_trap_dst))
		cJSON_AddItemToObject(config, "snmp_trap_dst", cJSON_CreateString(ipaddr_ntoa(&cfg->snmp_trap_dst)));
	if (cfg->ssh_active)
		cJSON_AddItemToObject(config, "ssh_active", cJSON_CreateNumber(cfg->ssh_active));
	if (cfg->ssh_auth != true)
		cJSON_AddItemToObject(config, "ssh_auth", cJSON_CreateNumber(cfg->ssh_auth));
	if (cfg->ssh_port > 0)
		cJSON_AddItemToObject(config, "ssh_port", cJSON_CreateNumber(cfg->ssh_port));
	if (strlen(cfg->ssh_user) > 0)
		cJSON_AddItemToObject(config, "ssh_user", cJSON_CreateString(cfg->ssh_user));
	if (strlen(cfg->ssh_pwhash) > 0)
		cJSON_AddItemToObject(config, "ssh_pwhash", cJSON_CreateString(cfg->ssh_pwhash));
	if ((o = sshpubkeys2json(cfg->ssh_pub_keys))) {
		cJSON_AddItemToObject(config, "ssh_pubkeys", o);
	}
	if ((o = acllist2json(cfg->ssh_acls, SSH_MAX_ACL_ENTRIES))) {
		cJSON_AddItemToObject(config, "ssh_acls", o);
	}
#endif

	/* Fan outputs */
	fans = cJSON_CreateArray();
	if (!fans)
		goto panic;
	for (i = 0; i < FAN_COUNT; i++) {
		const struct fan_output *f = &cfg->fans[i];

		o = cJSON_CreateObject();
		if (!o)
			goto panic;
		cJSON_AddItemToObject(o, "id", cJSON_CreateNumber(i));
		cJSON_AddItemToObject(o, "name", cJSON_CreateString(f->name));
		cJSON_AddItemToObject(o, "min_pwm", cJSON_CreateNumber(f->min_pwm));
		cJSON_AddItemToObject(o, "max_pwm", cJSON_CreateNumber(f->max_pwm));
		cJSON_AddItemToObject(o, "pwm_coefficient", cJSON_CreateNumber(f->pwm_coefficient));
		cJSON_AddItemToObject(o, "source_type", cJSON_CreateString(pwm_source2str(f->s_type)));
		cJSON_AddItemToObject(o, "source_id", cJSON_CreateNumber(f->s_id));
		cJSON_AddItemToObject(o, "pwm_map", pwm_map2json(&f->map));
		cJSON_AddItemToObject(o, "filter", filter2json(f->filter, f->filter_ctx));
		cJSON_AddItemToObject(o, "rpm_mode", cJSON_CreateString(rpm_mode2str(f->rpm_mode)));
		cJSON_AddItemToObject(o, "rpm_factor", cJSON_CreateNumber(f->rpm_factor));
		cJSON_AddItemToObject(o, "lra_low", cJSON_CreateNumber(f->lra_low));
		cJSON_AddItemToObject(o, "lra_high", cJSON_CreateNumber(f->lra_high));
		cJSON_AddItemToObject(o, "tach_hyst", cJSON_CreateNumber(f->tacho_hyst));
		cJSON_AddItemToObject(o, "pwm_hyst", cJSON_CreateNumber(f->pwm_hyst));
		cJSON_AddItemToArray(fans, o);
	}
	cJSON_AddItemToObject(config, "fans", fans);

	/* MB Fan inputs */
	mbfans = cJSON_CreateArray();
	if (!mbfans)
		goto panic;
	for (i = 0; i < MBFAN_COUNT; i++) {
		const struct mb_input *m = &cfg->mbfans[i];

		o = cJSON_CreateObject();
		if (!o)
			goto panic;
		cJSON_AddItemToObject(o, "id", cJSON_CreateNumber(i));
		cJSON_AddItemToObject(o, "name", cJSON_CreateString(m->name));
		cJSON_AddItemToObject(o, "min_rpm", cJSON_CreateNumber(m->min_rpm));
		cJSON_AddItemToObject(o, "max_rpm", cJSON_CreateNumber(m->max_rpm));
		cJSON_AddItemToObject(o, "rpm_mode", cJSON_CreateString(rpm_mode2str(m->rpm_mode)));
		cJSON_AddItemToObject(o, "rpm_coefficient", cJSON_CreateNumber(m->rpm_coefficient));
		cJSON_AddItemToObject(o, "rpm_factor", cJSON_CreateNumber(m->rpm_factor));
		cJSON_AddItemToObject(o, "lra_treshold", cJSON_CreateNumber(m->lra_treshold));
		if (m->lra_invert != false)
			cJSON_AddItemToObject(o, "lra_invert", cJSON_CreateNumber(m->lra_invert));
		cJSON_AddItemToObject(o, "source_type", cJSON_CreateString(tacho_source2str(m->s_type)));
		cJSON_AddItemToObject(o, "source_id", cJSON_CreateNumber(m->s_id));
		if (m->s_type == TACHO_MIN || m->s_type == TACHO_MAX || m->s_type == TACHO_AVG)
			cJSON_AddItemToObject(o, "sources", tacho_sources2json(m->sources));
		cJSON_AddItemToObject(o, "rpm_map", tacho_map2json(&m->map));
		cJSON_AddItemToObject(o, "filter", filter2json(m->filter, m->filter_ctx));
		cJSON_AddItemToArray(mbfans, o);
	}
	cJSON_AddItemToObject(config, "mbfans", mbfans);

	/* Sensor inputs */
	sensors = cJSON_CreateArray();
	if (!sensors)
		goto panic;
	for (i = 0; i < SENSOR_COUNT; i++) {
		const struct sensor_input *s = &cfg->sensors[i];

		o = cJSON_CreateObject();
		if (!o)
			goto panic;
		cJSON_AddItemToObject(o, "id", cJSON_CreateNumber(i));
		cJSON_AddItemToObject(o, "name", cJSON_CreateString(s->name));
		cJSON_AddItemToObject(o, "sensor_type", cJSON_CreateNumber(s->type));
		cJSON_AddItemToObject(o, "temp_offset", cJSON_CreateNumber(s->temp_offset));
		cJSON_AddItemToObject(o, "temp_coefficient", cJSON_CreateNumber(s->temp_coefficient));
		cJSON_AddItemToObject(o, "temp_map", temp_map2json(&s->map));
		if (s->type == TEMP_EXTERNAL) {
			cJSON_AddItemToObject(o, "temperature_nominal",
					cJSON_CreateNumber(s->temp_nominal));
			cJSON_AddItemToObject(o, "thermistor_nominal",
					cJSON_CreateNumber(s->thermistor_nominal));
			cJSON_AddItemToObject(o, "beta_coefficient",
					cJSON_CreateNumber(s->beta_coefficient));
		}
		cJSON_AddItemToObject(o, "filter", filter2json(s->filter, s->filter_ctx));
		cJSON_AddItemToArray(sensors, o);
	}
	cJSON_AddItemToObject(config, "sensors", sensors);

	/* Virtual Sensors */
	vsensors = cJSON_CreateArray();
	if (!vsensors)
		goto panic;
	for (i = 0; i < VSENSOR_COUNT; i++) {
		const struct vsensor_input *s = &cfg->vsensors[i];

		o = cJSON_CreateObject();
		if (!o)
			goto panic;
		cJSON_AddItemToObject(o, "id", cJSON_CreateNumber(i));
		cJSON_AddItemToObject(o, "name", cJSON_CreateString(s->name));
		cJSON_AddItemToObject(o, "mode", cJSON_CreateString(vsmode2str(s->mode)));
		cJSON_AddItemToObject(o, "temp_map", temp_map2json(&s->map));
		if (s->mode == VSMODE_MANUAL) {
			cJSON_AddItemToObject(o, "default_temp", cJSON_CreateNumber(s->default_temp));
			cJSON_AddItemToObject(o, "timeout", cJSON_CreateNumber(s->timeout));
		} else if (s->mode == VSMODE_ONEWIRE) {
			cJSON_AddItemToObject(o, "onewire_addr",
					cJSON_CreateString(onewireaddr2str(s->onewire_addr)));
		} else if (s->mode == VSMODE_I2C) {
			cJSON_AddItemToObject(o, "i2c_type",
					cJSON_CreateString(i2c_sensor_type_str(s->i2c_type)));
			cJSON_AddItemToObject(o, "i2c_addr",
					cJSON_CreateNumber(s->i2c_addr));
		} else {
			cJSON_AddItemToObject(o, "sensors", vsensors2json(s->sensors));
		}
		cJSON_AddItemToObject(o, "filter", filter2json(s->filter, s->filter_ctx));
		cJSON_AddItemToArray(vsensors, o);
	}
	cJSON_AddItemToObject(config, "vsensors", vsensors);

	return config;

panic:
	cJSON_Delete(config);
	return NULL;
}


int json_to_config(cJSON *config, struct fanpico_config *cfg)
{
	cJSON *ref, *item, *r;
	int id;
	const char *name, *val;


	if (!config || !cfg)
		return -1;


	/* Parse JSON configuration */

	if ((ref = cJSON_GetObjectItem(config, "id")))
		log_msg(LOG_INFO, "Config version: %s", ref->valuestring);
	if ((ref = cJSON_GetObjectItem(config, "debug")))
		set_debug_level(cJSON_GetNumberValue(ref));
	if ((ref = cJSON_GetObjectItem(config, "log_level")))
		set_log_level(cJSON_GetNumberValue(ref));
	if ((ref = cJSON_GetObjectItem(config, "syslog_level")))
		set_syslog_level(cJSON_GetNumberValue(ref));
	if ((ref = cJSON_GetObjectItem(config, "local_echo")))
		cfg->local_echo = (cJSON_IsTrue(ref) ? true : false);
	if ((ref = cJSON_GetObjectItem(config, "led_mode")))
		cfg->led_mode = cJSON_GetNumberValue(ref);
	if ((ref = cJSON_GetObjectItem(config, "spi_active")))
		cfg->spi_active = cJSON_GetNumberValue(ref);
	if ((ref = cJSON_GetObjectItem(config, "serial_active")))
		cfg->serial_active = cJSON_GetNumberValue(ref);
	if ((ref = cJSON_GetObjectItem(config, "onewire_active")))
		cfg->onewire_active = cJSON_GetNumberValue(ref);
	if ((ref = cJSON_GetObjectItem(config, "i2c_speed")))
		cfg->i2c_speed = cJSON_GetNumberValue(ref);
	if ((ref = cJSON_GetObjectItem(config, "adc_vref")))
	    cfg->adc_vref = cJSON_GetNumberValue(ref);
	if ((ref = cJSON_GetObjectItem(config, "display_type"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->display_type, val, sizeof(cfg->display_type));
	}
	if ((ref = cJSON_GetObjectItem(config, "display_theme"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->display_theme, val, sizeof(cfg->display_theme));
	}
	if ((ref = cJSON_GetObjectItem(config, "display_logo"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->display_logo, val, sizeof(cfg->display_logo));
	}
	if ((ref = cJSON_GetObjectItem(config, "display_layout_r"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->display_layout_r, val, sizeof(cfg->display_layout_r));
	}
	if ((ref = cJSON_GetObjectItem(config, "name"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->name, val, sizeof(cfg->name));
	}
	if ((ref = cJSON_GetObjectItem(config, "timezone"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->timezone, val, sizeof(cfg->timezone));
	}

#ifdef WIFI_SUPPORT
	uint32_t m;

	if ((ref = cJSON_GetObjectItem(config, "hostname"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->hostname, val, sizeof(cfg->hostname));
	}
	if ((ref = cJSON_GetObjectItem(config, "wifi_country"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->wifi_country, val, sizeof(cfg->wifi_country));
	}
	if ((ref = cJSON_GetObjectItem(config, "wifi_ssid"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->wifi_ssid, val, sizeof(cfg->wifi_ssid));
	}
	if ((ref = cJSON_GetObjectItem(config, "wifi_passwd"))) {
		if ((val = cJSON_GetStringValue(ref))) {
			char *p = base64decode(val);
			if (p) {
				strncopy(cfg->wifi_passwd, p, sizeof(cfg->wifi_passwd));
				free(p);
			}
		}
	}
	if ((ref = cJSON_GetObjectItem(config, "wifi_auth_mode"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->wifi_auth_mode, val, sizeof(cfg->wifi_auth_mode));
	}
	if ((ref = cJSON_GetObjectItem(config, "wifi_mode"))) {
		cfg->wifi_mode = cJSON_GetNumberValue(ref);
	}
	if ((ref = cJSON_GetObjectItem(config, "dns_servers"))) {
		json2iplist(ref, cfg->dns_servers, DNS_MAX_SERVERS);
	}
	if ((ref = cJSON_GetObjectItem(config, "syslog_server"))) {
		if ((val = cJSON_GetStringValue(ref)))
			ipaddr_aton(val, &cfg->syslog_server);
	}
	if ((ref = cJSON_GetObjectItem(config, "ntp_server"))) {
		if ((val = cJSON_GetStringValue(ref)))
			ipaddr_aton(val, &cfg->ntp_server);
	}
	if ((ref = cJSON_GetObjectItem(config, "ip"))) {
		if ((val = cJSON_GetStringValue(ref)))
			ipaddr_aton(val, &cfg->ip);
	}
	if ((ref = cJSON_GetObjectItem(config, "netmask"))) {
		if ((val = cJSON_GetStringValue(ref)))
			ipaddr_aton(val, &cfg->netmask);
	}
	if ((ref = cJSON_GetObjectItem(config, "gateway"))) {
		if ((val = cJSON_GetStringValue(ref)))
			ipaddr_aton(val, &cfg->gateway);
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_server"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->mqtt_server, val, sizeof(cfg->mqtt_server));
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_port"))) {
		cfg->mqtt_port = cJSON_GetNumberValue(ref);
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_tls"))) {
		cfg->mqtt_tls = cJSON_GetNumberValue(ref);
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_allow_scpi"))) {
		cfg->mqtt_allow_scpi = cJSON_GetNumberValue(ref);
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_user"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->mqtt_user, val, sizeof(cfg->mqtt_user));
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_pass"))) {
		if ((val = cJSON_GetStringValue(ref))) {
			char *p = base64decode(val);
			if (p) {
				strncopy(cfg->mqtt_pass, p, sizeof(cfg->mqtt_pass));
				free(p);
			}
		}
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_status_topic"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->mqtt_status_topic, val, sizeof(cfg->mqtt_status_topic));
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_cmd_topic"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->mqtt_cmd_topic, val, sizeof(cfg->mqtt_cmd_topic));
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_resp_topic"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->mqtt_resp_topic, val, sizeof(cfg->mqtt_resp_topic));
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_status_interval"))) {
		cfg->mqtt_status_interval = cJSON_GetNumberValue(ref);
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_temp_interval"))) {
		cfg->mqtt_temp_interval = cJSON_GetNumberValue(ref);
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_vsensor_interval"))) {
		cfg->mqtt_vsensor_interval = cJSON_GetNumberValue(ref);
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_rpm_interval"))) {
		cfg->mqtt_rpm_interval = cJSON_GetNumberValue(ref);
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_duty_interval"))) {
		cfg->mqtt_duty_interval = cJSON_GetNumberValue(ref);
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_temp_mask"))) {
		if (!str_to_bitmask(cJSON_GetStringValue(ref), SENSOR_MAX_COUNT, &m, 1))
			cfg->mqtt_temp_mask = m;
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_vtemp_mask"))) {
		if (!str_to_bitmask(cJSON_GetStringValue(ref), VSENSOR_MAX_COUNT, &m, 1))
			cfg->mqtt_vtemp_mask = m;
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_vhumidity_mask"))) {
		if (!str_to_bitmask(cJSON_GetStringValue(ref), VSENSOR_MAX_COUNT, &m, 1))
			cfg->mqtt_vhumidity_mask = m;
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_vpressure_mask"))) {
		if (!str_to_bitmask(cJSON_GetStringValue(ref), VSENSOR_MAX_COUNT, &m, 1))
			cfg->mqtt_vpressure_mask = m;
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_fan_rpm_mask"))) {
		if (!str_to_bitmask(cJSON_GetStringValue(ref), FAN_MAX_COUNT, &m, 1))
			cfg->mqtt_fan_rpm_mask = m;
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_fan_duty_mask"))) {
		if (!str_to_bitmask(cJSON_GetStringValue(ref), FAN_MAX_COUNT, &m, 1))
			cfg->mqtt_fan_duty_mask = m;
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_mbfan_rpm_mask"))) {
		if (!str_to_bitmask(cJSON_GetStringValue(ref), MBFAN_MAX_COUNT, &m, 1))
			cfg->mqtt_mbfan_rpm_mask = m;
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_mbfan_duty_mask"))) {
		if (!str_to_bitmask(cJSON_GetStringValue(ref), MBFAN_MAX_COUNT, &m, 1))
			cfg->mqtt_mbfan_duty_mask = m;
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_temp_topic"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->mqtt_temp_topic, val, sizeof(cfg->mqtt_temp_topic));
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_vtemp_topic"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->mqtt_vtemp_topic, val, sizeof(cfg->mqtt_vtemp_topic));
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_vhumidity_topic"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->mqtt_vhumidity_topic, val, sizeof(cfg->mqtt_vhumidity_topic));
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_vpressure_topic"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->mqtt_vpressure_topic, val, sizeof(cfg->mqtt_vpressure_topic));
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_fan_rpm_topic"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->mqtt_fan_rpm_topic, val, sizeof(cfg->mqtt_fan_rpm_topic));
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_fan_duty_topic"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->mqtt_fan_duty_topic, val, sizeof(cfg->mqtt_fan_duty_topic));
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_mbfan_rpm_topic"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->mqtt_mbfan_rpm_topic, val, sizeof(cfg->mqtt_mbfan_rpm_topic));
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_mbfan_duty_topic"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->mqtt_mbfan_duty_topic, val, sizeof(cfg->mqtt_mbfan_duty_topic));
	}
	if ((ref = cJSON_GetObjectItem(config, "mqtt_ha_discovery_prefix"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->mqtt_ha_discovery_prefix, val, sizeof(cfg->mqtt_ha_discovery_prefix));
	}
	if ((ref = cJSON_GetObjectItem(config, "telnet_active"))) {
		cfg->telnet_active = cJSON_GetNumberValue(ref);
	}
	if ((ref = cJSON_GetObjectItem(config, "telnet_auth"))) {
		cfg->telnet_auth = cJSON_GetNumberValue(ref);
	}
	if ((ref = cJSON_GetObjectItem(config, "telnet_raw_mode"))) {
		cfg->telnet_raw_mode = cJSON_GetNumberValue(ref);
	}
	if ((ref = cJSON_GetObjectItem(config, "telnet_port"))) {
		cfg->telnet_port = cJSON_GetNumberValue(ref);
	}
	if ((ref = cJSON_GetObjectItem(config, "telnet_user"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->telnet_user, val, sizeof(cfg->telnet_user));
	}
	if ((ref = cJSON_GetObjectItem(config, "telnet_pwhash"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->telnet_pwhash, val, sizeof(cfg->telnet_pwhash));
	}
	if ((ref = cJSON_GetObjectItem(config, "telnet_acls"))) {
		json2acllist(ref, cfg->telnet_acls, TELNET_MAX_ACL_ENTRIES);
	}
	if ((ref = cJSON_GetObjectItem(config, "snmp_active"))) {
		cfg->snmp_active = cJSON_GetNumberValue(ref);
	}
	if ((ref = cJSON_GetObjectItem(config, "snmp_community"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->snmp_community, val, sizeof(cfg->snmp_community));
	}
	if ((ref = cJSON_GetObjectItem(config, "snmp_community_write"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->snmp_community_write, val, sizeof(cfg->snmp_community_write));
	}
	if ((ref = cJSON_GetObjectItem(config, "snmp_contact"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->snmp_contact, val, sizeof(cfg->snmp_contact));
	}
	if ((ref = cJSON_GetObjectItem(config, "snmp_location"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->snmp_location, val, sizeof(cfg->snmp_location));
	}
	if ((ref = cJSON_GetObjectItem(config, "snmp_community_trap"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->snmp_community_trap, val, sizeof(cfg->snmp_community_trap));
	}
	if ((ref = cJSON_GetObjectItem(config, "snmp_auth_traps"))) {
		cfg->snmp_auth_traps = cJSON_GetNumberValue(ref);
	}
	if ((ref = cJSON_GetObjectItem(config, "snmp_trap_dst"))) {
		if ((val = cJSON_GetStringValue(ref)))
			ipaddr_aton(val, &cfg->snmp_trap_dst);
	}
	if ((ref = cJSON_GetObjectItem(config, "ssh_active"))) {
		cfg->ssh_active = cJSON_GetNumberValue(ref);
	}
	if ((ref = cJSON_GetObjectItem(config, "ssh_auth"))) {
		cfg->ssh_auth = cJSON_GetNumberValue(ref);
	}
	if ((ref = cJSON_GetObjectItem(config, "ssh_port"))) {
		cfg->ssh_port = cJSON_GetNumberValue(ref);
	}
	if ((ref = cJSON_GetObjectItem(config, "ssh_user"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->ssh_user, val, sizeof(cfg->ssh_user));
	}
	if ((ref = cJSON_GetObjectItem(config, "ssh_pwhash"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->ssh_pwhash, val, sizeof(cfg->ssh_pwhash));
	}
	if ((ref = cJSON_GetObjectItem(config, "ssh_pubkeys"))) {
		json2sshpubkeys(ref, cfg->ssh_pub_keys);
	}
	if ((ref = cJSON_GetObjectItem(config, "ssh_acls"))) {
		json2acllist(ref, cfg->ssh_acls, SSH_MAX_ACL_ENTRIES);
	}
#endif

	/* Fan output configurations */
	ref = cJSON_GetObjectItem(config, "fans");
	cJSON_ArrayForEach(item, ref) {
		id = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(item, "id"));
		if (id >= 0 && id < FAN_COUNT) {
			struct fan_output *f = &cfg->fans[id];

			name = cJSON_GetStringValue(cJSON_GetObjectItem(item, "name"));
			if (name) strncopy(f->name, name ,sizeof(f->name));

			if ((r = cJSON_GetObjectItem(item, "min_pwm")))
				f->min_pwm = cJSON_GetNumberValue(r);
			if ((r = cJSON_GetObjectItem(item, "max_pwm")))
				f->max_pwm = cJSON_GetNumberValue(r);
			if ((r = cJSON_GetObjectItem(item, "pwm_coefficient")))
				f->pwm_coefficient = cJSON_GetNumberValue(r);
			f->s_type = str2pwm_source(cJSON_GetStringValue(
							cJSON_GetObjectItem(item, "source_type")));
			f->s_id = cJSON_GetNumberValue(cJSON_GetObjectItem(item, "source_id"));
			if ((r = cJSON_GetObjectItem(item, "pwm_map")))
				json2pwm_map(r, &f->map);
			f->rpm_mode = str2rpm_mode(cJSON_GetStringValue(
							cJSON_GetObjectItem(item, "rpm_mode")));;
			if ((r = cJSON_GetObjectItem(item,"rpm_factor")))
				f->rpm_factor = cJSON_GetNumberValue(r);
			if ((r = cJSON_GetObjectItem(item,"lra_low")))
				f->lra_low = cJSON_GetNumberValue(r);
			if ((r = cJSON_GetObjectItem(item,"lra_high")))
				f->lra_high = cJSON_GetNumberValue(r);
			if ((r = cJSON_GetObjectItem(item, "filter")))
				json2filter(r, &f->filter, &f->filter_ctx);
			if ((r = cJSON_GetObjectItem(item, "tach_hyst")))
				f->tacho_hyst = cJSON_GetNumberValue(r);
			if ((r = cJSON_GetObjectItem(item, "pwm_hyst")))
				f->pwm_hyst = cJSON_GetNumberValue(r);
		}
	}

	/* MB Fan input configurations */
	ref = cJSON_GetObjectItem(config, "mbfans");
	cJSON_ArrayForEach(item, ref) {
		id = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(item, "id"));
		if (id >= 0 && id < MBFAN_COUNT) {
			struct mb_input *m = &cfg->mbfans[id];

			name = cJSON_GetStringValue(cJSON_GetObjectItem(item, "name"));
			if (name) strncopy(m->name, name ,sizeof(m->name));

			if ((r = cJSON_GetObjectItem(item, "min_rpm")))
				m->min_rpm = cJSON_GetNumberValue(r);
			if ((r = cJSON_GetObjectItem(item, "max_rpm")))
				m->max_rpm = cJSON_GetNumberValue(r);
			m->rpm_mode = str2rpm_mode(cJSON_GetStringValue(
							cJSON_GetObjectItem(item, "rpm_mode")));;
			if ((r = cJSON_GetObjectItem(item, "rpm_coefficient")))
				m->rpm_coefficient = cJSON_GetNumberValue(r);
			if ((r = cJSON_GetObjectItem(item,"rpm_factor")))
				m->rpm_factor = cJSON_GetNumberValue(r);
			if ((r = cJSON_GetObjectItem(item,"lra_treshold")))
				m->lra_treshold = cJSON_GetNumberValue(r);
			if ((r = cJSON_GetObjectItem(item,"lra_invert")))
				m->lra_invert = cJSON_GetNumberValue(r);
			m->s_type = str2tacho_source(cJSON_GetStringValue(
							cJSON_GetObjectItem(item, "source_type")));
			m->s_id = cJSON_GetNumberValue(cJSON_GetObjectItem(item, "source_id"));
			if ((r = cJSON_GetObjectItem(item, "sources")))
				json2tacho_sources(r, m->sources);
			if ((r = cJSON_GetObjectItem(item, "rpm_map")))
				json2tacho_map(r, &m->map);
			if ((r = cJSON_GetObjectItem(item, "filter")))
				json2filter(r, &m->filter, &m->filter_ctx);
		}
	}

	/* Sensor input configurations */
	ref = cJSON_GetObjectItem(config, "sensors");
	cJSON_ArrayForEach(item, ref) {
		id = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(item, "id"));
		if (id >= 0 && id < SENSOR_COUNT) {
			struct sensor_input *s = &cfg->sensors[id];

			name = cJSON_GetStringValue(cJSON_GetObjectItem(item, "name"));
			if (name) strncopy(s->name, name ,sizeof(s->name));

			s->type = cJSON_GetNumberValue(cJSON_GetObjectItem(item, "sensor_type"));
			if (s->type == TEMP_EXTERNAL) {
				s->temp_nominal = cJSON_GetNumberValue(
					cJSON_GetObjectItem(item, "temperature_nominal"));
				s->thermistor_nominal = cJSON_GetNumberValue(
					cJSON_GetObjectItem(item, "thermistor_nominal"));
				s->beta_coefficient = cJSON_GetNumberValue(
					cJSON_GetObjectItem(item, "beta_coefficient"));
			}
			s->temp_offset = cJSON_GetNumberValue(
				cJSON_GetObjectItem(item, "temp_offset"));
			s->temp_coefficient = cJSON_GetNumberValue(
				cJSON_GetObjectItem(item, "temp_coefficient"));
			if ((r = cJSON_GetObjectItem(item, "temp_map")))
				json2temp_map(r, &s->map);
			if ((r = cJSON_GetObjectItem(item, "filter")))
				json2filter(r, &s->filter, &s->filter_ctx);
		}
	}

	/* Virtual Sensor configurations */
	ref = cJSON_GetObjectItem(config, "vsensors");
	cJSON_ArrayForEach(item, ref) {
		id = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(item, "id"));
		if (id >= 0 && id < VSENSOR_COUNT) {
			struct vsensor_input *s = &cfg->vsensors[id];

			name = cJSON_GetStringValue(cJSON_GetObjectItem(item, "name"));
			if (name) strncopy(s->name, name ,sizeof(s->name));

			s->mode = str2vsmode(cJSON_GetStringValue(cJSON_GetObjectItem(item, "mode")));
			if (s->mode == VSMODE_MANUAL) {
				if ((r = cJSON_GetObjectItem(item, "default_temp")))
					s->default_temp = cJSON_GetNumberValue(r);
				if ((r = cJSON_GetObjectItem(item, "timeout")))
					s->timeout = cJSON_GetNumberValue(r);
			} else if (s->mode == VSMODE_ONEWIRE) {
				if ((r = cJSON_GetObjectItem(item, "onewire_addr")))
					s->onewire_addr = str2onewireaddr(cJSON_GetStringValue(r));
			} else if (s->mode == VSMODE_I2C) {
				if ((r = cJSON_GetObjectItem(item, "i2c_type")))
					s->i2c_type = get_i2c_sensor_type(cJSON_GetStringValue(r));
				if ((r = cJSON_GetObjectItem(item, "i2c_addr")))
					s->i2c_addr = cJSON_GetNumberValue(r);
			} else {
				if ((r = cJSON_GetObjectItem(item, "sensors")))
					json2vsensors(r, s->sensors);
			}
			if ((r = cJSON_GetObjectItem(item, "temp_map")))
				json2temp_map(r, &s->map);
			if ((r = cJSON_GetObjectItem(item, "filter")))
				json2filter(r, &s->filter, &s->filter_ctx);
		}
	}

	return 0;
}


void read_config(bool use_default_config)
{
	const char *default_config = fanpico_default_config;
	uint32_t default_config_size = strlen(default_config);
	cJSON *config = NULL;
	int res;
	uint32_t file_size;
	char  *buf = NULL;


	if (!use_default_config) {
		log_msg(LOG_INFO, "Reading configuration...");

		res = flash_read_file(&buf, &file_size, "fanpico.cfg");
		if (res == 0 && buf != NULL) {
			/* parse saved config... */
			config = cJSON_Parse(buf);
			if (!config) {
				const char *error_str = cJSON_GetErrorPtr();
				log_msg(LOG_ERR, "Failed to parse saved config: %s",
					(error_str ? error_str : "") );
			}
			free(buf);
		}
	}

	if (!config) {
		log_msg(LOG_NOTICE, "Using default configuration...");
		log_msg(LOG_DEBUG, "config size = %lu", default_config_size);
		/* printf("default config:\n---\n%s\n---\n", default_config); */
		config = cJSON_Parse(fanpico_default_config);
		if (!config) {
			const char *error_str = cJSON_GetErrorPtr();
			panic("Failed to parse default config: %s\n",
				(error_str ? error_str : "") );
		}
	}


        /* Parse JSON configuration */
	mutex_enter_blocking(config_mutex);
	clear_config(&fanpico_config);
	if (json_to_config(config, &fanpico_config) < 0) {
		log_msg(LOG_ERR, "Error parsing JSON configuration");
	}
	if (use_default_config) {
		/* Enable more verbose logging if in "safe-mode" ... */
		set_log_level(LOG_INFO);
		fanpico_config.local_echo = true;
	}
	mutex_exit(config_mutex);

	cJSON_Delete(config);
}


void save_config()
{
	cJSON *config;
	char *str;

	log_msg(LOG_NOTICE, "Saving configuration...");

	config = config_to_json(cfg);
	if (!config) {
		log_msg(LOG_ALERT, "Out of memory!");
		return;
	}

	if ((str = cJSON_Print(config)) == NULL) {
		log_msg(LOG_ERR, "Failed to generate JSON output");
	} else {
		uint32_t config_size = strlen(str) + 1;
		flash_write_file(str, config_size, "fanpico.cfg");
		free(str);
	}

	cJSON_Delete(config);
}


void print_config()
{
	cJSON *config;
	char *str;

	config = config_to_json(cfg);
	if (!config) {
		log_msg(LOG_ALERT, "Out of memory");
		return;
	}

	if ((str = cJSON_Print(config)) == NULL) {
		log_msg(LOG_ERR, "Failed to generate JSON output");
	} else {
		printf("Current Configuration:\n%s\n\n", str);
		free(str);
	}

	cJSON_Delete(config);
}




#define CONFIG_READ_TIMEOUT 10000 // 10s
#define CONFIG_READ_BUF_SIZE 2048
#define BLANK_LINE_COUNT 2  // Number of blank lines to signify end of config...

void upload_config()
{
	absolute_time_t t_timeout = get_absolute_time();
	cJSON *config = NULL;
	cJSON *ref;
	char *buf = NULL;
	char tmp[256];
	uint32_t buf_len = CONFIG_READ_BUF_SIZE * 3;
	uint32_t buf_used = 0;
	int state = 0;
	int blank_count = 0;


	if (!(buf = malloc(buf_len))) {
		log_msg(LOG_ERR,"upload_config(): not enough memory (%lu)", buf_len);
		return;
	}

	tmp[0] = 0;
	printf("Paste FanPico configuration in JSON format:\n");
	while (1) {
		char *line;
		uint32_t line_len;
		int r;

		if ((r = getstring_timeout_ms(tmp, sizeof(tmp), 100)) < 0)
			break;
		if (r > 0) {
			line = trim_str(tmp);
			line_len = strnlen(line, sizeof(tmp));

			blank_count = (line_len ? 0 : blank_count + 1);
			if (blank_count >= BLANK_LINE_COUNT)
				break;

			if (state == 0) {
				if (line_len)
					state = 1;
				else
					continue;
			}
			if (state == 1) {
				if (!line_len) {
					if (blank_count >= 1)
						break;
					continue;
				}
				if ((buf_len - buf_used) < line_len + 1) {
					char *new_buf;

					buf_len += CONFIG_READ_BUF_SIZE;
					if (!(new_buf = realloc(buf, buf_len))) {
						log_msg(LOG_ERR,"upload_config(): not enough memory (%lu)", buf_len);
						goto panic;
					}
					buf = new_buf;
				}
				memcpy(&buf[buf_used], line, line_len);
				buf_used += line_len;
				buf[buf_used++] = '\n';
				tmp[0] = 0;
			}
		}
		if (time_passed(&t_timeout, CONFIG_READ_TIMEOUT)) {
			printf("Timeout!\n");
			break;
		}
	}

	buf[buf_used] = 0;
	printf("[Received %lu bytes]\n\n", buf_used);

	if (buf_used < 1) {
		printf("No configuration received.\n");
		goto panic;
	}
	if (!(config = cJSON_Parse(buf))) {
		printf("Failed to parse uploaded config");
		goto panic;
	}
	if (!(ref = cJSON_GetObjectItem(config, "id"))) {
		printf("Uploaded JSON object missing 'id' field.\n");
		goto panic;
	}
	if (strncmp(ref->valuestring, "fanpico-config-v", 16)) {
		printf("Invalid configuration uploaded.\n");
		goto panic;
	}

	printf("Clearing config...\n");
	clear_config(&fanpico_config);
	printf("Loading config...\n");
	if (json_to_config(config, &fanpico_config) < 0) {
		printf("Error parsing JSON configuration\n");
	} else {
		printf("Configuration successfully loaded.\n");
	}

panic:
	if (buf)
		free(buf);
	if (config)
		cJSON_Delete(config);
}

void delete_config()
{
	int res;

	res = flash_delete_file("fanpico.cfg");
	if (res) {
		log_msg(LOG_ERR, "Failed to delete configuration.");
	}
}
