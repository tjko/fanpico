/* config.c
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
#include <malloc.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "cJSON.h"
#include "pico_hal.h"
#ifdef WIFI_SUPPORT
#include "lwip/ip_addr.h"
#endif

#include "fanpico.h"

/* default_config.s */
extern const char fanpico_default_config[];


struct fanpico_config config;
struct fanpico_config *cfg = &config;


int str2pwm_source(const char *s)
{
	int ret = 0;

	if (!strncasecmp(s, "fixed", 5))
		ret = PWM_FIXED;
	else if (!strncasecmp(s, "mbfan", 5))
		ret = PWM_MB;
	else if (!strncasecmp(s, "sensor", 6))
		ret = PWM_SENSOR;
	else if (!strncasecmp(s, "fan", 3))
		ret = PWM_FAN;

	return ret;
}


const char* pwm_source2str(enum pwm_source_types source)
{
	if (source == PWM_MB)
		return "mbfan";
	else if (source == PWM_SENSOR)
		return "sensor";
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
	case PWM_FAN:
		ret = (s_id >= 0 && s_id < FAN_MAX_COUNT ? 1 : 0);
		break;
	}

	return ret;
}


int str2tacho_source(const char *s)
{
	int ret = 0;

	if (!strncasecmp(s, "fixed", 5))
		ret = TACHO_FIXED;
	else if (!strncasecmp(s, "fan", 2))
		ret = TACHO_FAN;

	return ret;
}


const char* tacho_source2str(enum tacho_source_types source)
{
	if (source == TACHO_FAN)
		return "fan";

	return "fixed";
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
		ret = (s_id >= 0 && s_id < FAN_MAX_COUNT ? 1 : 0);
		break;
	}

	return ret;
}


void json2pwm_map(cJSON *item, struct pwm_map *map)
{
	cJSON *row;
	int c = 0;

	cJSON_ArrayForEach(row, item) {
		map->pwm[c][0] = cJSON_GetNumberValue(cJSON_GetArrayItem(row, 0));
		map->pwm[c][1] = cJSON_GetNumberValue(cJSON_GetArrayItem(row, 1));
		c++;
	}

	map->points = c;
}


cJSON* pwm_map2json(struct pwm_map *map)
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


void json2tacho_map(cJSON *item, struct tacho_map *map)
{
	cJSON *row;
	int c = 0;

	cJSON_ArrayForEach(row, item) {
		map->tacho[c][0] = cJSON_GetNumberValue(cJSON_GetArrayItem(row, 0));
		map->tacho[c][1] = cJSON_GetNumberValue(cJSON_GetArrayItem(row, 1));
		c++;
	}

	map->points = c;
}


cJSON* tacho_map2json(struct tacho_map *map)
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


void json2temp_map(cJSON *item, struct temp_map *map)
{
	cJSON *row;
	int c = 0;

	cJSON_ArrayForEach(row, item) {
		map->temp[c][0] = cJSON_GetNumberValue(cJSON_GetArrayItem(row, 0));
		map->temp[c][1] = cJSON_GetNumberValue(cJSON_GetArrayItem(row, 1));
		c++;
	}

	map->points = c;
}


cJSON* temp_map2json(struct temp_map *map)
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


void clear_config(struct fanpico_config *cfg)
{
	int i;
	struct sensor_input *s;
	struct fan_output *f;
	struct mb_input *m;

	for (i = 0; i < SENSOR_MAX_COUNT; i++) {
		s=&cfg->sensors[i];

		s->name[0]=0;
		s->type = TEMP_INTERNAL;
		s->thermistor_nominal = 0.0;
		s->beta_coefficient = 0.0;
		s->temp_nominal = 0.0;
		s->temp_offset=0.0;
		s->temp_coefficient=0.0;
		s->map.points=0;
	}

	for (i = 0; i < FAN_MAX_COUNT; i++) {
		f=&cfg->fans[i];

		f->name[0]=0;
		f->min_pwm=0;
		f->max_pwm=0;
		f->pwm_coefficient=0.0;
		f->s_type=PWM_FIXED;
		f->s_id=0;
		f->map.points=0;
		f->rpm_factor=2;
	}

	for (i = 0; i < MBFAN_MAX_COUNT; i++) {
		m=&cfg->mbfans[i];

		m->name[0]=0;
		m->min_rpm=0;
		m->max_rpm=0;
		m->rpm_coefficient=0.0;
		m->rpm_factor=2;
		m->s_type=TACHO_FIXED;
		m->s_id=0;
		m->map.points=0;
	}

	cfg->local_echo = false;
	cfg->led_mode = 0;
	strncopy(cfg->display_type, "default", sizeof(cfg->display_type));
#ifdef WIFI_SUPPORT
	cfg->wifi_ssid[0] = 0;
	cfg->wifi_passwd[0] = 0;
	strncopy(cfg->wifi_country, "XX", sizeof(cfg->wifi_country));
	ip_addr_set_any(0, &cfg->syslog_server);
	ip_addr_set_any(0, &cfg->ntp_server);
	ip_addr_set_any(0, &cfg->ip);
	ip_addr_set_any(0, &cfg->netmask);
	ip_addr_set_any(0, &cfg->gateway);
#endif
}


cJSON *config_to_json(struct fanpico_config *cfg)
{
	cJSON *config = cJSON_CreateObject();
	cJSON *fans, *mbfans, *sensors, *o;
	int i;

	if (!config)
		return NULL;

	cJSON_AddItemToObject(config, "id", cJSON_CreateString("fanpico-config-v1"));
	cJSON_AddItemToObject(config, "debug", cJSON_CreateNumber(get_debug_level()));
	cJSON_AddItemToObject(config, "local_echo", cJSON_CreateBool(cfg->local_echo));
	cJSON_AddItemToObject(config, "led_mode", cJSON_CreateNumber(cfg->led_mode));
	if (strlen(cfg->display_type) > 0)
		cJSON_AddItemToObject(config, "display_type", cJSON_CreateString(cfg->display_type));

#ifdef WIFI_SUPPORT
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
#endif

	/* Fan outputs */
	fans = cJSON_CreateArray();
	if (!fans)
		goto panic;
	for (i = 0; i < FAN_COUNT; i++) {
		struct fan_output *f = &cfg->fans[i];

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
		cJSON_AddItemToObject(o, "rpm_factor", cJSON_CreateNumber(f->rpm_factor));
		cJSON_AddItemToArray(fans, o);
	}
	cJSON_AddItemToObject(config, "fans", fans);

	/* MB Fan inputs */
	mbfans = cJSON_CreateArray();
	if (!mbfans)
		goto panic;
	for (i = 0; i < MBFAN_COUNT; i++) {
		struct mb_input *m = &cfg->mbfans[i];

		o = cJSON_CreateObject();
		if (!o)
			goto panic;
		cJSON_AddItemToObject(o, "id", cJSON_CreateNumber(i));
		cJSON_AddItemToObject(o, "name", cJSON_CreateString(m->name));
		cJSON_AddItemToObject(o, "min_rpm", cJSON_CreateNumber(m->min_rpm));
		cJSON_AddItemToObject(o, "max_rpm", cJSON_CreateNumber(m->max_rpm));
		cJSON_AddItemToObject(o, "rpm_coefficient", cJSON_CreateNumber(m->rpm_coefficient));
		cJSON_AddItemToObject(o, "rpm_factor", cJSON_CreateNumber(m->rpm_factor));
		cJSON_AddItemToObject(o, "source_type", cJSON_CreateString(tacho_source2str(m->s_type)));
		cJSON_AddItemToObject(o, "source_id", cJSON_CreateNumber(m->s_id));
		cJSON_AddItemToObject(o, "rpm_map", tacho_map2json(&m->map));
		cJSON_AddItemToArray(mbfans, o);
	}
	cJSON_AddItemToObject(config, "mbfans", mbfans);

	/* Sensor inputs */
	sensors = cJSON_CreateArray();
	if (!sensors)
		goto panic;
	for (i = 0; i < SENSOR_COUNT; i++) {
		struct sensor_input *s = &cfg->sensors[i];

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
		cJSON_AddItemToArray(sensors, o);
	}
	cJSON_AddItemToObject(config, "sensors", sensors);

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
		printf("Config version: %s\n", ref->valuestring);
	if ((ref = cJSON_GetObjectItem(config, "debug")))
		set_debug_level(cJSON_GetNumberValue(ref));
	if ((ref = cJSON_GetObjectItem(config, "local_echo")))
		cfg->local_echo = (cJSON_IsTrue(ref) ? true : false);
	if ((ref = cJSON_GetObjectItem(config, "led_mode")))
		cfg->led_mode = cJSON_GetNumberValue(ref);
	if ((ref = cJSON_GetObjectItem(config, "display_type"))) {
		if ((val = cJSON_GetStringValue(ref)))
			strncopy(cfg->display_type, val, sizeof(cfg->display_type));
	}

#ifdef WIFI_SUPPORT
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
#endif

	/* Fan output configurations */
	ref = cJSON_GetObjectItem(config, "fans");
	cJSON_ArrayForEach(item, ref) {
		id = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(item, "id"));
		if (id >= 0 && id < FAN_COUNT) {
			struct fan_output *f = &cfg->fans[id];

			name = cJSON_GetStringValue(cJSON_GetObjectItem(item, "name"));
			if (name) strncopy(f->name, name ,sizeof(f->name));

			f->min_pwm = cJSON_GetNumberValue(cJSON_GetObjectItem(item, "min_pwm"));
			f->max_pwm = cJSON_GetNumberValue(cJSON_GetObjectItem(item, "max_pwm"));
			f->pwm_coefficient = cJSON_GetNumberValue(
				cJSON_GetObjectItem(item,"pwm_coefficient"));
			f->s_type = str2pwm_source(cJSON_GetStringValue(
							cJSON_GetObjectItem(item, "source_type")));
			f->s_id = cJSON_GetNumberValue(cJSON_GetObjectItem(item, "source_id"));
			if ((r = cJSON_GetObjectItem(item, "pwm_map")))
				json2pwm_map(r, &f->map);
			f->rpm_factor = cJSON_GetNumberValue(cJSON_GetObjectItem(item,"rpm_factor"));
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

			m->min_rpm = cJSON_GetNumberValue(cJSON_GetObjectItem(item, "min_rpm"));
			m->max_rpm = cJSON_GetNumberValue(cJSON_GetObjectItem(item, "max_rpm"));
			m->rpm_coefficient = cJSON_GetNumberValue(
				cJSON_GetObjectItem(item, "rpm_coefficient"));
			m->rpm_factor = cJSON_GetNumberValue(cJSON_GetObjectItem(item,"rpm_factor"));
			m->s_type = str2tacho_source(cJSON_GetStringValue(
							cJSON_GetObjectItem(item, "source_type")));
			m->s_id = cJSON_GetNumberValue(cJSON_GetObjectItem(item, "source_id"));
			if ((r = cJSON_GetObjectItem(item, "rpm_map")))
				json2tacho_map(r, &m->map);
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
		}
	}

	return 0;
}


void read_config(bool multicore)
{
	const char *default_config = fanpico_default_config;
	uint32_t default_config_size = strlen(default_config);
	cJSON *config = NULL;
	int res, fd;
	int32_t file_size, bytes_read;


	printf("Reading configuration...\n");

	if (multicore)
		multicore_lockout_start_blocking();

	/* Mount flash filesystem... */
	if ((res = pico_mount(false)) < 0) {
		printf("Mount failed: %d (%s)\n", res, pico_errmsg(res));
		printf("Trying to initialize a new filesystem...\n");
		if ((res = pico_mount(true)) < 0) {
			printf("Unable to initialize flash filesystem!\n");
		} else {
			printf("Filesystem successfully initialized. (%d)\n", res);
		}
	} else {
		printf("Filesystem mounted OK\n");
		/* Read configuration file... */
		if ((fd = pico_open("fanpico.cfg", LFS_O_RDONLY)) < 0) {
			printf("Cannot open fanpico.cfg: %d (%s)\n", fd, pico_errmsg(fd));
		} else {
			file_size = pico_size(fd);
			printf("Configuration file opened ok: %li bytes\n", file_size);
			if (file_size > 0) {
				char *buf = malloc(file_size);
				if (!buf) {
					printf("Not enough memory!\n");
				} else {
					// read saved config...
					printf("Reading saved configuration...\n");
					bytes_read = pico_read(fd, buf, file_size);
					if (bytes_read < file_size) {
						printf("Error reading configuration file: %li\n", bytes_read);
					} else {
						//printf("bytes read: %li\n", bytes_read);
						//printf("---\n%s\n---\n", buf);

						// parse saved config...
						config = cJSON_Parse(buf);
						if (!config) {
							const char *error_str = cJSON_GetErrorPtr();
							printf("Failed to parse saved config: %s\n",
								(error_str ? error_str : "") );
						}
					}
					free(buf);
				}
			}
			pico_close(fd);
		}
		pico_unmount();
	}

	if (multicore)
		multicore_lockout_end_blocking();

	if (!config) {
		printf("Using default configuration...\n");
		printf("config size = %lu\n", default_config_size);
		/* printf("default config:\n---\n%s\n---\n", default_config); */
		config = cJSON_Parse(fanpico_default_config);
		if (!config) {
			const char *error_str = cJSON_GetErrorPtr();
			panic("Failed to parse default config: %s\n",
				(error_str ? error_str : "") );
		}
	}


        /* Parse JSON configuration */
	clear_config(cfg);
	if (json_to_config(config, cfg) < 0) {
		printf("Error parsing JSON configuration\n");
	}

	cJSON_Delete(config);
}


void save_config()
{
	cJSON *config;
	char *str;
	int res, fd;

	printf("Saving configuration...\n");

	config = config_to_json(cfg);
	if (!config) {
		printf("Out of memory?");
		return;
	}

	if ((str = cJSON_Print(config)) == NULL) {
		printf("Failed to generate JSON output\n");
	} else {
		uint32_t config_size = strlen(str) + 1;

		multicore_lockout_start_blocking();

		/* Mount flash filesystem... */
		if ((res = pico_mount(false)) < 0) {
			printf("Mount failed: %d (%s)\n", res, pico_errmsg(res));
		} else {
			if ((fd = pico_open("fanpico.cfg", LFS_O_WRONLY | LFS_O_CREAT)) < 0) {
				printf("Failed to create configuration file: %d (%s)\n",
					fd, pico_errmsg(fd));
			} else {
				lfs_size_t wrote = pico_write(fd, str, config_size);
				if (wrote < config_size) {
					printf("Failed to write configuration to file: %li\n", wrote);
				} else {
					printf("Configuration successfully saved: %li bytes\n", wrote);
				}
				pico_close(fd);
			}
			pico_unmount();
		}

		multicore_lockout_end_blocking();
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
		printf("Out of memory?");
		return;
	}

	if ((str = cJSON_Print(config)) == NULL) {
		printf("Failed to generate JSON output\n");
	} else {
		printf("Current Configuration:\n%s\n---\n", str);
		free(str);
	}

	cJSON_Delete(config);
}


void delete_config()
{
	int res;
	struct lfs_info stat;

	multicore_lockout_start_blocking();

	/* Mount flash filesystem... */
	if ((res = pico_mount(false)) < 0) {
		printf("Mount failed: %d (%s)\n", res, pico_errmsg(res));
	} else {
		/* Check configuration file exists... */
		if ((res = pico_stat("fanpico.cfg", &stat)) < 0) {
			printf("Configuration file not found: %d (%s)\n", res, pico_errmsg(res));
		} else {
			/* Remove configuration file...*/
			printf("Removing configuration file (%lu bytes)\n", stat.size);
			if ((res = pico_remove("fanpico.cfg")) < 0) {
				printf("Failed to remove file: %d (%s)\n", res, pico_errmsg(res));
			}
		}
		pico_unmount();
	}

	multicore_lockout_end_blocking();
}
