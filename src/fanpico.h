/* fanpico.h
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

#ifndef FANPICO_H
#define FANPICO_H 1

#include "config.h"
#include "log.h"
#include <time.h>
#ifdef LIB_PICO_CYW43_ARCH
#define WIFI_SUPPORT 1
#include "lwip/ip_addr.h"
#endif

#ifndef FANPICO_MODEL
#error unknown board model
#endif

#define FAN_MAX_COUNT     8   /* Max Number of Fan outputs on the board */
#define MBFAN_MAX_COUNT   4   /* Max Number of (Motherboard) Fan inputs on the board */
#define SENSOR_MAX_COUNT  3   /* Max Number of sensor inputs on the board */

#define SENSOR_SERIES_RESISTANCE 10000.0

#define ADC_REF_VOLTAGE 3.0
#define ADC_MAX_VALUE   (1 << 12)
#define ADC_AVG_WINDOW  10

#define MAX_NAME_LEN   64
#define MAX_MAP_POINTS 32
#define MAX_GPIO_PINS  32

#define WIFI_SSID_MAX_LEN    32
#define WIFI_PASSWD_MAX_LEN  64



enum pwm_source_types {
	PWM_FIXED   = 0,     /* fixed speed set by s_id */
	PWM_MB      = 1,     /* mb pwm signal */
	PWM_SENSOR  = 2,     /* sensor signal */
	PWM_FAN     = 3,     /* another fan */
};

enum tacho_source_types {
	TACHO_FIXED  = 0,     /* fixed speed set by s_id */
	TACHO_FAN    = 1,     /* fan tacho signal */
};

enum temp_sensor_types {
	TEMP_INTERNAL = 0,
	TEMP_EXTERNAL = 1,
};

struct pwm_map {
	uint8_t points;
	uint8_t pwm[MAX_MAP_POINTS][2];
};

struct tacho_map {
	uint8_t points;
	uint16_t tacho[MAX_MAP_POINTS][2];
};

struct temp_map {
	uint8_t points;
	float temp[MAX_MAP_POINTS][2];
};

struct fan_output {
	char name[MAX_NAME_LEN];

	/* output PWM signal settings */
	uint8_t min_pwm;
	uint8_t max_pwm;
	float pwm_coefficient;
	enum pwm_source_types s_type;
	uint16_t s_id;
	struct pwm_map map;

	/* input Tacho signal settings */
	uint8_t rpm_factor;
};

struct mb_input {
	char name[MAX_NAME_LEN];

	/* output Tacho signal settings */
	uint16_t min_rpm;
	uint16_t max_rpm;
	float rpm_coefficient;
	uint8_t rpm_factor;
	enum tacho_source_types s_type;
	uint16_t s_id;
	struct tacho_map map;
};

struct sensor_input {
	enum temp_sensor_types type;
	char name[MAX_NAME_LEN];
	float thermistor_nominal;
	float temp_nominal;
	float beta_coefficient;
	float temp_offset;
	float temp_coefficient;
	struct temp_map map;
};

struct fanpico_config {
	struct sensor_input sensors[SENSOR_MAX_COUNT];
	struct fan_output fans[FAN_MAX_COUNT];
	struct mb_input mbfans[MBFAN_MAX_COUNT];
	bool local_echo;
	uint8_t led_mode;
	char display_type[64];
	char name[32];
#ifdef WIFI_SUPPORT
	char wifi_ssid[WIFI_SSID_MAX_LEN + 1];
	char wifi_passwd[WIFI_PASSWD_MAX_LEN + 1];
	char wifi_country[4];
	ip_addr_t syslog_server;
	ip_addr_t ntp_server;
	ip_addr_t ip;
	ip_addr_t netmask;
	ip_addr_t gateway;
#endif
};

struct fanpico_state {
	/* inputs */
	float mbfan_duty[MBFAN_MAX_COUNT];
	float fan_freq[FAN_MAX_COUNT];
	float temp[SENSOR_MAX_COUNT];
	/* outputs */
	float fan_duty[FAN_MAX_COUNT];
	float mbfan_freq[MBFAN_MAX_COUNT];
};


/* fanpico.c */
void print_mallinfo();
extern const struct fanpico_state *fanpico_state;

/* bi_decl.c */
void set_binary_info();

/* command.c */
void process_command(struct fanpico_state *state, struct fanpico_config *config, char *command);
int cmd_version(const char *cmd, const char *args, int query, char *prev_cmd);

/* config.c */
extern struct fanpico_config *cfg;
int str2pwm_source(const char *s);
const char* pwm_source2str(enum pwm_source_types source);
int valid_pwm_source_ref(enum pwm_source_types source, uint16_t s_id);
int str2tacho_source(const char *s);
const char* tacho_source2str(enum tacho_source_types source);
int valid_tacho_source_ref(enum tacho_source_types source, uint16_t s_id);
void read_config(bool multicore);
void save_config();
void delete_config();
void print_config();

/* display.c */
void display_init();
void clear_display();
void display_status(const struct fanpico_state *state, const struct fanpico_config *config);

/* network.c */
void network_init();
void network_mac();
void network_poll();
void network_status();
void set_pico_system_time(long unsigned int sec);

/* pwm.c */
extern float mbfan_pwm_duty[MBFAN_MAX_COUNT];
void setup_pwm_inputs();
void setup_pwm_outputs();
void set_pwm_duty_cycle(uint fan, float duty);
float get_pwm_duty_cycle(uint fan);
void get_pwm_duty_cycles();
double pwm_map(struct pwm_map *map, double val);
double calculate_pwm_duty(struct fanpico_state *state, struct fanpico_config *config, int i);

/* sensors.c */
double get_temperature(uint8_t input);
double sensor_get_duty(struct sensor_input *sensor, double temp);

/* tacho.c */
void setup_tacho_inputs();
void setup_tacho_outputs();
void read_tacho_inputs();
void update_tacho_input_freq(struct fanpico_state *state);
void set_tacho_output_freq(uint fan, double frequency);
double tacho_map(struct tacho_map *map, double val);
double calculate_tacho_freq(struct fanpico_state *state, struct fanpico_config *config, int i);

/* util.c */
void log_msg(int priority, const char *format, ...);
int get_debug_level();
void set_debug_level(int level);
int get_log_level();
void set_log_level(int level);
int get_syslog_level();
void set_syslog_level(int level);
void debug(int debug_level, const char *fmt, ...);
void print_mallinfo();
char *trim_str(char *s);
const char *rp2040_model_str();
const char *mac_address_str(const uint8_t *mac);
int check_for_change(double oldval, double newval, double treshold);
int time_passed(absolute_time_t *t, uint32_t us);
char* base64encode(const char *input);
char* base64decode(const char *input);
char *strncopy(char *dst, const char *src, size_t len);
datetime_t *tm_to_datetime(const struct tm *tm, datetime_t *t);
struct tm *datetime_to_tm(const datetime_t *t, struct tm *tm);
time_t datetime_to_time(const datetime_t *datetime);

/* crc32.c */
unsigned int xcrc32 (const unsigned char *buf, int len, unsigned int init);


#endif /* FANPICO_H */
