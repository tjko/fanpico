/* fanpico.h
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

#ifndef FANPICO_H
#define FANPICO_H 1

#include "config.h"
#include "log.h"
#include <time.h>
#include "pico/mutex.h"
#ifdef LIB_PICO_CYW43_ARCH
#define WIFI_SUPPORT 1
#include "lwip/ip_addr.h"
#include "lwip/apps/sntp.h"
#endif

#ifndef FANPICO_MODEL
#error unknown board model
#endif

#define FAN_MAX_COUNT     8   /* Max number of Fan outputs on the board */
#define MBFAN_MAX_COUNT   4   /* Max number of (Motherboard) Fan inputs on the board */
#define SENSOR_MAX_COUNT  3   /* Max number of sensor inputs on the board */
#define VSENSOR_MAX_COUNT 8   /* Max number of virtual sensors */

#define VSENSOR_SOURCE_MAX_COUNT 8
#define VSENSOR_COUNT            8

#define ONEWIRE_MAX_COUNT        8

#define SENSOR_SERIES_RESISTANCE 10000.0

#define ADC_REF_VOLTAGE 3.0
#define FAN_TACHO_HYSTERESIS 1.0
#define FAN_PWM_HYSTERESIS 1.0
#define ADC_MAX_VALUE   (1 << 12)
#define ADC_AVG_WINDOW  10

#define MAX_NAME_LEN   64
#define MAX_MAP_POINTS 32
#define MAX_GPIO_PINS  32

#define WIFI_SSID_MAX_LEN     32
#define WIFI_PASSWD_MAX_LEN   64
#define WIFI_COUNTRY_MAX_LEN  3

#define MQTT_MAX_TOPIC_LEN            48
#define MQTT_MAX_USERNAME_LEN         80
#define MQTT_MAX_PASSWORD_LEN         64
#define DEFAULT_MQTT_STATUS_INTERVAL  600
#define DEFAULT_MQTT_TEMP_INTERVAL    60
#define DEFAULT_MQTT_RPM_INTERVAL     60
#define DEFAULT_MQTT_DUTY_INTERVAL    60

#define TELNET_MAX_ACL_ENTRIES 4
#define SSH_MAX_ACL_ENTRIES 4
#define SSH_MAX_PUB_KEYS  4
#define MAX_USERNAME_LEN  16
#define MAX_PWHASH_LEN    128

#ifdef NDEBUG
#define WATCHDOG_ENABLED      1
#define WATCHDOG_REBOOT_DELAY 8000
#endif

#ifndef I2C_DEFAULT_SPEED
#define I2C_DEFAULT_SPEED   1000000L /* Default I2C bus frequency 1MHz */
#endif
#ifndef SPI_DEFAULT_SPEED
#define SPI_DEFAULT_SPEED  30000000L /* Default SPI bus frequency 30MHz */
#endif

#define FANPICO_FS_SIZE  (256*1024)
#define FANPICO_FS_OFFSET  (PICO_FLASH_SIZE_BYTES - FANPICO_FS_SIZE)


enum pwm_source_types {
	PWM_FIXED   = 0,     /* Fixed speed set by s_id */
	PWM_MB      = 1,     /* Mb pwm signal */
	PWM_SENSOR  = 2,     /* Sensor signal */
	PWM_FAN     = 3,     /* Another fan */
	PWM_VSENSOR = 4,     /* Virtual sensor */
};
#define PWM_SOURCE_ENUM_MAX 4

enum signal_filter_types {
	FILTER_NONE      = 0, /* No filtering */
	FILTER_LOSSYPEAK = 1, /* "Lossy Peak Detector" with time decay */
	FILTER_SMA       = 2, /* Simple moving average */
};
#define FILTER_ENUM_MAX 2

enum tacho_source_types {
	TACHO_FIXED  = 0,     /* Fixed speed set by s_id */
	TACHO_FAN    = 1,     /* Fan tacho signal */
	TACHO_MIN    = 2,     /* Slowest tacho signal from a group of fans. */
	TACHO_MAX    = 3,     /* Fastest tacho signal from a group of fans. */
	TACHO_AVG    = 4,     /* Average tacho signal from a group of fans. */
};
#define TACHO_ENUM_MAX 4

enum temp_sensor_types {
	TEMP_INTERNAL = 0,
	TEMP_EXTERNAL = 1,
};
#define TEMP_ENUM_MAX 1

enum vsensor_modes {
	VSMODE_MANUAL  = 0,
	VSMODE_MAX     = 1,
	VSMODE_MIN     = 2,
	VSMODE_AVG     = 3,
	VSMODE_DELTA   = 4,
	VSMODE_ONEWIRE = 5,
	VSMODE_I2C     = 6,
};
#define VSMODE_ENUM_MAX 6

enum rpm_modes {
	RMODE_TACHO = 0,  /* Normal Tachometer signal */
	RMODE_LRA   = 1,  /* Locked Rotor Alarm signal */
};
#define RPMMODE_ENUM_MAX 1

#ifdef WIFI_SUPPORT
typedef struct acl_entry_t {
	ip_addr_t ip;
	uint8_t prefix;
} acl_entry_t;
#endif

struct ssh_public_key {
	char username[MAX_USERNAME_LEN + 1];
	char type[32 + 1];
	char name[32 + 1];
	uint8_t pubkey[128];
	uint16_t pubkey_size;
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
	float tacho_hyst;
	float pwm_hyst;

	/* output PWM signal settings */
	uint8_t min_pwm;
	uint8_t max_pwm;
	float pwm_coefficient;
	enum pwm_source_types s_type;
	uint16_t s_id;
	struct pwm_map map;
	enum signal_filter_types filter;
	void *filter_ctx;

	/* input Tacho signal settings */
	uint8_t rpm_mode;
	uint8_t rpm_factor;
	uint16_t lra_low;
	uint16_t lra_high;
};

struct mb_input {
	char name[MAX_NAME_LEN];

	/* output Tacho signal settings */
	uint8_t rpm_mode;
	uint16_t min_rpm;
	uint16_t max_rpm;
	float rpm_coefficient;
	uint8_t rpm_factor;
	uint16_t lra_treshold;
	bool lra_invert;
	enum tacho_source_types s_type;
	uint16_t s_id;
	uint8_t sources[FAN_MAX_COUNT];
	struct tacho_map map;

	/* input PWM signal settings */
	enum signal_filter_types filter;
	void *filter_ctx;
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
	enum signal_filter_types filter;
	void *filter_ctx;
};

struct vsensor_input {
	char name[MAX_NAME_LEN];
	uint8_t mode;
	float default_temp;
	int32_t timeout;
	uint8_t sensors[VSENSOR_SOURCE_MAX_COUNT];
	uint64_t onewire_addr;
	uint8_t i2c_type;
	uint8_t i2c_addr;
	struct temp_map map;
	enum signal_filter_types filter;
	void *filter_ctx;
};

struct fanpico_config {
	struct sensor_input sensors[SENSOR_MAX_COUNT];
	struct vsensor_input vsensors[VSENSOR_MAX_COUNT];
	struct fan_output fans[FAN_MAX_COUNT];
	struct mb_input mbfans[MBFAN_MAX_COUNT];
	bool local_echo;
	uint8_t led_mode;
	char display_type[64];
	char display_theme[16];
	char display_logo[16];
	char display_layout_r[64];
	char name[32];
	char timezone[64];
	bool spi_active;
	bool serial_active;
	bool onewire_active;
	uint32_t i2c_speed;
	float adc_vref;
#ifdef WIFI_SUPPORT
	char wifi_ssid[WIFI_SSID_MAX_LEN + 1];
	char wifi_passwd[WIFI_PASSWD_MAX_LEN + 1];
	char wifi_country[WIFI_COUNTRY_MAX_LEN + 1];
	char wifi_auth_mode[16];
	uint8_t wifi_mode;
	char hostname[32];
	ip_addr_t dns_servers[DNS_MAX_SERVERS];
	ip_addr_t syslog_server;
	ip_addr_t ntp_server;
	ip_addr_t ip;
	ip_addr_t netmask;
	ip_addr_t gateway;
	char mqtt_server[32];
	uint32_t mqtt_port;
	bool mqtt_tls;
	bool mqtt_allow_scpi;
	char mqtt_user[MQTT_MAX_USERNAME_LEN + 1];
	char mqtt_pass[MQTT_MAX_PASSWORD_LEN + 1];
	char mqtt_status_topic[MQTT_MAX_TOPIC_LEN + 1];
	uint32_t mqtt_status_interval;
	char mqtt_cmd_topic[MQTT_MAX_TOPIC_LEN + 1];
	char mqtt_resp_topic[MQTT_MAX_TOPIC_LEN + 1];
	uint16_t mqtt_temp_mask;
	uint16_t mqtt_vtemp_mask;
	uint16_t mqtt_vhumidity_mask;
	uint16_t mqtt_vpressure_mask;
	uint16_t mqtt_fan_rpm_mask;
	uint16_t mqtt_fan_duty_mask;
	uint16_t mqtt_mbfan_rpm_mask;
	uint16_t mqtt_mbfan_duty_mask;
	char mqtt_temp_topic[MQTT_MAX_TOPIC_LEN + 1];
	char mqtt_vtemp_topic[MQTT_MAX_TOPIC_LEN + 1];
	char mqtt_vhumidity_topic[MQTT_MAX_TOPIC_LEN + 1];
	char mqtt_vpressure_topic[MQTT_MAX_TOPIC_LEN + 1];
	char mqtt_fan_rpm_topic[MQTT_MAX_TOPIC_LEN + 1];
	char mqtt_fan_duty_topic[MQTT_MAX_TOPIC_LEN + 1];
	char mqtt_mbfan_rpm_topic[MQTT_MAX_TOPIC_LEN + 1];
	char mqtt_mbfan_duty_topic[MQTT_MAX_TOPIC_LEN + 1];
	uint32_t mqtt_temp_interval;
	uint32_t mqtt_vsensor_interval;
	uint32_t mqtt_rpm_interval;
	uint32_t mqtt_duty_interval;
	char mqtt_ha_discovery_prefix[32 + 1];
	bool telnet_active;
	bool telnet_auth;
	bool telnet_raw_mode;
	uint32_t telnet_port;
	char telnet_user[MAX_USERNAME_LEN + 1];
	char telnet_pwhash[MAX_PWHASH_LEN + 1];
	acl_entry_t telnet_acls[TELNET_MAX_ACL_ENTRIES];
	bool snmp_active;
	char snmp_community[SNMP_MAX_COMMUNITY_STR_LEN + 1];
	char snmp_community_write[SNMP_MAX_COMMUNITY_STR_LEN + 1];
	char snmp_contact[32 + 1];
	char snmp_location[32 + 1];
	char snmp_community_trap[SNMP_MAX_COMMUNITY_STR_LEN + 1];
	bool snmp_auth_traps;
	ip_addr_t snmp_trap_dst;
	bool ssh_active;
	bool ssh_auth;
	uint32_t ssh_port;
	char ssh_user[MAX_USERNAME_LEN + 1];
	char ssh_pwhash[MAX_PWHASH_LEN + 1];
	struct ssh_public_key ssh_pub_keys[SSH_MAX_PUB_KEYS];
	acl_entry_t ssh_acls[SSH_MAX_ACL_ENTRIES];
#endif
	/* Non-config items */
	float vtemp[VSENSOR_MAX_COUNT];
	float vhumidity[VSENSOR_MAX_COUNT];
	float vpressure[VSENSOR_MAX_COUNT];
	absolute_time_t vtemp_updated[VSENSOR_MAX_COUNT];
	void *i2c_context[VSENSOR_MAX_COUNT];
};

/* Firmware settings that can be modified with picotool */
struct fanpico_fw_settings {
	bool safemode;      /* Safe mode disables loading saved configuration during boot. */
	int bootdelay;      /* Delay (seconds) after initializing USB console during boot. */
	int sysclock;       /* Set system clock in MHz (overclocking) */
};

#if WIFI_SUPPORT
struct fanpico_network_state {
	ip_addr_t ip;
	ip_addr_t netmask;
	ip_addr_t gateway;
	ip_addr_t syslog_server;
	ip_addr_t ntp_servers[SNTP_MAX_SERVERS];
	ip_addr_t dns_servers[DNS_MAX_SERVERS];
	uint8_t mac[6];
	char hostname[32];
	bool netif_up;
	int wifi_status;
	absolute_time_t wifi_status_change;
};
#endif


struct fanpico_state {
	/* inputs */
	float mbfan_duty[MBFAN_MAX_COUNT];
	float mbfan_duty_prev[MBFAN_MAX_COUNT];
	float fan_freq[FAN_MAX_COUNT];
	float fan_freq_prev[FAN_MAX_COUNT];
	float temp[SENSOR_MAX_COUNT];
	float temp_prev[SENSOR_MAX_COUNT];
	float vtemp[VSENSOR_MAX_COUNT];
	float vhumidity[VSENSOR_MAX_COUNT];
	float vpressure[VSENSOR_MAX_COUNT];
	absolute_time_t vtemp_updated[VSENSOR_MAX_COUNT];
	float vtemp_prev[VSENSOR_MAX_COUNT];
	float onewire_temp[ONEWIRE_MAX_COUNT];
	absolute_time_t onewire_temp_updated[VSENSOR_MAX_COUNT];
	float prev_onewire_temp[ONEWIRE_MAX_COUNT];
	/* outputs */
	float fan_duty[FAN_MAX_COUNT];
	float fan_duty_prev[FAN_MAX_COUNT];
	float mbfan_freq[MBFAN_MAX_COUNT];
	float mbfan_freq_prev[MBFAN_MAX_COUNT];
};

/* Memory structure that persists over soft resets */
struct persistent_memory_block {
	uint32_t id;
	uint32_t len;
	struct timespec saved_time;
	uint64_t uptime;
	uint64_t prev_uptime;
	uint64_t total_uptime;
	uint32_t warmstart;
	char timezone[64];
	uint32_t crc32;
};


/* fanpico.c */
extern struct persistent_memory_block *persistent_mem;
extern const struct fanpico_state *fanpico_state;
#if WIFI_SUPPORT
extern struct fanpico_network_state *net_state;
#endif
extern bool rebooted_by_watchdog;
extern mutex_t *state_mutex;
void update_display_state();
void update_persistent_memory();
void update_persistent_memory_tz(const char *tz);

/* bi_decl.c */
void set_binary_info(struct fanpico_fw_settings *settings);

/* command.c */
void process_command(const struct fanpico_state *state, struct fanpico_config *config, char *command);
int last_command_status();

/* config.c */
extern mutex_t *config_mutex;
extern const struct fanpico_config *cfg;
int str2pwm_source(const char *s);
const char* pwm_source2str(enum pwm_source_types source);
int str2vsmode(const char *s);
const char* vsmode2str(enum vsensor_modes mode);
int str2rpm_mode(const char *s);
const char* rpm_mode2str(enum rpm_modes mode);
int valid_pwm_source_ref(enum pwm_source_types source, uint16_t s_id);
int str2tacho_source(const char *s);
const char* tacho_source2str(enum tacho_source_types source);
int valid_tacho_source_ref(enum tacho_source_types source, uint16_t s_id);
void read_config(bool use_default_config);
void save_config();
void delete_config();
void print_config();
void upload_config();

/* display.c */
void display_init();
void clear_display();
void display_message(int rows, const char **text_lines);
void display_status(const struct fanpico_state *state, const struct fanpico_config *config);

/* display_lcd.c */
void lcd_display_init();
void lcd_clear_display();
void lcd_display_status(const struct fanpico_state *state,const struct fanpico_config *conf);
void lcd_display_message(int rows, const char **text_lines);

/* display_oled.c */
void oled_display_init();
void oled_clear_display();
void oled_display_status(const struct fanpico_state *state, const struct fanpico_config *conf);
void oled_display_message(int rows, const char **text_lines);

/* flash.h */
void lfs_setup(bool multicore);
int flash_format(bool multicore);
int flash_read_file(char **bufptr, uint32_t *sizeptr, const char *filename);
int flash_write_file(const char *buf, uint32_t size, const char *filename);
int flash_delete_file(const char *filename);
int flash_rename_file(const char *oldname, const char *newname);
int flash_copy_file(const char *srcname, const char *dstname, bool overwrite);
int flash_file_size(const char *filename);
int flash_get_fs_info(size_t *size, size_t *free, size_t *files,
		size_t *directories, size_t *filesizetotal);
int flash_list_directory(const char *path, bool recursive);
void print_rp2040_flashinfo();


/* network.c */
#if WIFI_SUPPORT
bool wifi_get_auth_type(const char *name, uint32_t *type);
const char* wifi_auth_type_name(uint32_t type);
const char* wifi_link_status_text(int status);
void set_pico_system_time(long unsigned int sec);
void wifi_status();
void wifi_info_display();
void wifi_rejoin();
void wifi_mac();
#endif
void network_init();
void network_poll();
const char *network_ip();


#if WIFI_SUPPORT
/* httpd.c */
u16_t fanpico_ssi_handler(const char *tag, char *insert, int insertlen,
			u16_t current_tag_part, u16_t *next_tag_part);

/* mqtt.c */
void fanpico_setup_mqtt_client();
int fanpico_mqtt_client_active();
void fanpico_mqtt_reconnect();
void fanpico_mqtt_publish();
void fanpico_mqtt_publish_temp();
void fanpico_mqtt_publish_vsensor();
void fanpico_mqtt_publish_rpm();
void fanpico_mqtt_publish_duty();
void fanpico_mqtt_scpi_command();

/* telnetd.c */
void telnetserver_init();
void telnetserver_disconnect();
void telnetserver_who();

/* sshd.c */
void sshserver_init();
void sshserver_disconnect();
void sshserver_who();

/* ssh_util.c */
void ssh_list_pkeys();
int ssh_create_pkey(const char* args);
int ssh_delete_pkey(const char* args);
int ssh_get_pkey(int index, char** buf_ptr, uint32_t* buf_size_ptr, const char** name_ptr);
#if WIFI_SUPPORT
int str_to_ssh_pubkey(const char *s, struct ssh_public_key *pk);
const char* ssh_pubkey_to_str(const struct ssh_public_key *pk, char *s, size_t s_len);
#endif

/* snmp.c */
void fanpico_snmp_init();
void fanpico_snmp_startup_trap(bool warmstart);

#endif

/* onewire.c */
void setup_onewire_bus();
int onewire_read_temps(struct fanpico_config *config, struct fanpico_state *state);
uint64_t onewire_address(uint sensor);

/* i2c.c */
void scan_i2c_bus();
void display_i2c_status();
void setup_i2c_bus(struct fanpico_config *config);
int i2c_read_temps(struct fanpico_config *config);

/* tls.c */
int read_pem_file(char *buf, uint32_t size, uint32_t timeout, bool append);
#ifdef WIFI_SUPPORT
struct altcp_tls_config* tls_server_config();
#endif

/* pwm.c */
extern float mbfan_pwm_duty[MBFAN_MAX_COUNT];
void setup_pwm_inputs();
void setup_pwm_outputs();
void set_pwm_duty_cycle(uint fan, float duty);
float get_pwm_duty_cycle(uint fan);
void get_pwm_duty_cycles(const struct fanpico_config *config);
double pwm_map(const struct pwm_map *map, double val);
double calculate_pwm_duty(struct fanpico_state *state, const struct fanpico_config *config, int i);

/* filters.c */
int str2filter(const char *s);
const char* filter2str(enum signal_filter_types source);
void* filter_parse_args(enum signal_filter_types filter, char *args);
char* filter_print_args(enum signal_filter_types filter, void *ctx);
float filter(enum signal_filter_types filter, void *ctx, float input);

/* sensors.c */
double get_temperature(uint8_t input, const struct fanpico_config *config);
double sensor_get_duty(const struct temp_map *map, double temp);
double get_vsensor(uint8_t i, struct fanpico_config *config,
		struct fanpico_state *state);

/* tacho.c */
void setup_tacho_inputs();
void setup_tacho_input_interrupts();
void setup_tacho_outputs();
void read_tacho_inputs();
void update_tacho_input_freq(struct fanpico_state *state);
void set_tacho_output_freq(uint fan, double frequency);
void set_lra_output(uint fan, bool lra);
double tacho_map(const struct tacho_map *map, double val);
double calculate_tacho_freq(struct fanpico_state *state, const struct fanpico_config *config, int i);

/* log.c */
int str2log_priority(const char *pri);
const char* log_priority2str(int pri);
int str2log_facility(const char *facility);
const char* log_facility2str(int facility);
void log_msg(int priority, const char *format, ...);
int get_debug_level();
void set_debug_level(int level);
int get_log_level();
void set_log_level(int level);
int get_syslog_level();
void set_syslog_level(int level);
void debug(int debug_level, const char *fmt, ...);

/* util.c */
void print_mallinfo();
char *trim_str(char *s);
int str_to_int(const char *str, int *val, int base);
int str_to_float(const char *str, float *val);
time_t timespec_to_time_t(const struct timespec *ts);
struct timespec* time_t_to_timespec(time_t t, struct timespec *ts);
char* time_t_to_str(char *buf, size_t size, const time_t t);
char* uptime_to_str(char *buf, size_t size, uint64_t uptime, bool show_secs);
bool str_to_time_t(const char *str, time_t *t);
bool rtc_get_tm(struct tm *tm);
bool rtc_get_time(time_t *t);
int check_for_change(double oldval, double newval, double threshold);
int64_t pow_i64(int64_t x, uint8_t y);
double round_decimal(double val, unsigned int decimal);
char* base64encode_raw(const void *input, size_t input_len);
int base64decode_raw(const void *input, size_t input_len, void **output);
char* base64encode(const char *input);
char* base64decode(const char *input);
char *strncopy(char *dst, const char *src, size_t size);
char *strncatenate(char *dst, const char *src, size_t size);
int clamp_int(int val, int min, int max);
void* memmem(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen);
char *bitmask_to_str(uint32_t mask, uint16_t len, uint8_t base, bool range);
int str_to_bitmask(const char *str, uint16_t len, uint32_t *mask, uint8_t base);

/* util_rp2.c */
uint32_t get_stack_pointer();
uint32_t get_stack_free();
void print_rp2_meminfo();
void print_irqinfo();
#if PICO_SDK_VERSION_MAJOR < 2
void watchdog_disable();
#endif
const char *rp2_model_str();
const char *pico_serial_str();
int time_passed(absolute_time_t *t, uint32_t ms);
int time_elapsed(absolute_time_t t, uint32_t ms);
int getstring_timeout_ms(char *str, uint32_t maxlen, uint32_t timeout);


/* crc32.c */
unsigned int xcrc32 (const unsigned char *buf, int len, unsigned int init);

#endif /* FANPICO_H */
