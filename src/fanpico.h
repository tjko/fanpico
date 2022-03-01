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

#define FANPICO_VERSION   "1.0b"
#define FANPICO_MODEL     "0804"

#define FAN_MAX_COUNT     8   /* Number of Fan outputs on the board */
#define MBFAN_MAX_COUNT   4   /* Number of (Motherboard) Fan inputs on the board */
#define SENSOR_MAX_COUNT  1   /* Number of sensor inputs on the board */

#define MAX_NAME_LEN   64
#define MAX_MAP_POINTS 32

#define LED_PIN 25


/* Pins for Fan Signals */

#define FAN1_TACHO_READ_PIN 2
#define FAN2_TACHO_READ_PIN 3
#define FAN3_TACHO_READ_PIN 20
#define FAN4_TACHO_READ_PIN 21
#define FAN5_TACHO_READ_PIN 22
#define FAN6_TACHO_READ_PIN 26
#define FAN7_TACHO_READ_PIN 27
#define FAN8_TACHO_READ_PIN 27

#define FAN1_PWM_GEN_PIN 4  /* PWM2A */
#define FAN2_PWM_GEN_PIN 5  /* PWM2B */
#define FAN3_PWM_GEN_PIN 6  /* PWM3A */
#define FAN4_PWM_GEN_PIN 7  /* PWM3B */
#define FAN5_PWM_GEN_PIN 8  /* PWM4A */
#define FAN6_PWM_GEN_PIN 9  /* PWM4B */
#define FAN7_PWM_GEN_PIN 10 /* PWM5A */
#define FAN8_PWM_GEN_PIN 11 /* PWM5B */


/* Pins for Motherboar Fan Connector Signals */

#define MBFAN1_TACHO_GEN_PIN 12
#define MBFAN2_TACHO_GEN_PIN 14
#define MBFAN3_TACHO_GEN_PIN 16
#define MBFAN4_TACHO_GEN_PIN 18

#define MBFAN1_PWM_READ_PIN 13 /* PWM6B */
#define MBFAN2_PWM_READ_PIN 15 /* PWM7B */
#define MBFAN3_PWM_READ_PIN 17 /* PWM0B */
#define MBFAN4_PWM_READ_PIN 19 /* PWM1B */

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
	char name[MAX_NAME_LEN];
	float temp_offset;
	float temp_coefficient;
	struct temp_map map;
};

struct fanpico_config {
	struct sensor_input sensors[SENSOR_MAX_COUNT];
	struct fan_output fans[FAN_MAX_COUNT];
	struct mb_input mbfans[MBFAN_MAX_COUNT];
	bool local_echo;
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
void read_config();
void save_config();
void delete_config();
void print_config();


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
float get_pico_temp();
double sensor_get_temp(struct sensor_input *sensor, double temp);
double sensor_get_duty(struct sensor_input *sensor, double temp);

/* tacho.c */
extern float fan_tacho_freq[FAN_MAX_COUNT];
void setup_tacho_inputs();
void setup_tacho_outputs();
void update_tacho_input_freq();
void set_tacho_output_freq(uint fan, double frequency);
double tacho_map(struct tacho_map *map, double val);
double calculate_tacho_freq(struct fanpico_state *state, struct fanpico_config *config, int i);

/* util.c */
int get_debug_level();
void set_debug_level(int level);
void debug(int debug_level, const char *fmt, ...);
void print_mallinfo();
char *trim_str(char *s);


/* crc32.c */
unsigned int xcrc32 (const unsigned char *buf, int len, unsigned int init);


#endif /* FANPICO_H */
