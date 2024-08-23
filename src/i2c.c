/* i2c.c
   Copyright (C) 2024 Timo Kokkonen <tjko@iki.fi>

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
#include <math.h>
#include <assert.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#include "fanpico.h"
#include "i2c.h"



/* i2c_adt7410.c */
void* adt7410_init(i2c_inst_t *i2c, uint8_t addr);
int adt7410_start_measurement(void *ctx);
int adt7410_get_measurement(void *ctx, float *temp, float *pressure, float *humidity);

/* i2c_aht.c */
void* aht1x_init(i2c_inst_t *i2c, uint8_t addr);
void* aht2x_init(i2c_inst_t *i2c, uint8_t addr);
int aht_start_measurement(void *ctx);
int aht_get_measurement(void *ctx, float *temp, float *pressure, float *humidity);

/* i2c_bmp180.c */
void* bmp180_init(i2c_inst_t *i2c, uint8_t addr);
int bmp180_start_measurement(void *ctx);
int bmp180_get_measurement(void *ctx, float *temp, float *pressure, float *humidity);

/* i2c_bmp280.c */
void* bmp280_init(i2c_inst_t *i2c, uint8_t addr);
int bmp280_start_measurement(void *ctx);
int bmp280_get_measurement(void *ctx, float *temp, float *pressure, float *humidity);

/* i2c_dps310.c */
void* dps310_init(i2c_inst_t *i2c, uint8_t addr);
int dps310_start_measurement(void *ctx);
int dps310_get_measurement(void *ctx, float *temp, float *pressure, float *humidity);

/* i2c_mcp9808.c */
void* mcp9808_init(i2c_inst_t *i2c, uint8_t addr);
int mcp9808_start_measurement(void *ctx);
int mcp9808_get_measurement(void *ctx, float *temp, float *pressure, float *humidity);

/* i2c_pct2075.c */
void* pct2075_init(i2c_inst_t *i2c, uint8_t addr);
int pct2075_start_measurement(void *ctx);
int pct2075_get_measurement(void *ctx, float *temp, float *pressure, float *humidity);

/* i2c_tmp102.c */
void* tmp102_init(i2c_inst_t *i2c, uint8_t addr);
int tmp102_start_measurement(void *ctx);
int tmp102_get_measurement(void *ctx, float *temp, float *pressure, float *humidity);

/* i2c_tmp117.c */
void* tmp117_init(i2c_inst_t *i2c, uint8_t addr);
int tmp117_start_measurement(void *ctx);
int tmp117_get_measurement(void *ctx, float *temp, float *pressure, float *humidity);

static const i2c_sensor_entry_t i2c_sensor_types[] = {
	{ "NONE", NULL, NULL, NULL }, /* this needs to be first so that valid sensors have index > 0 */
	{ "ADT7410", adt7410_init, adt7410_start_measurement, adt7410_get_measurement },
	{ "AHT2x", aht2x_init, aht_start_measurement, aht_get_measurement },
	{ "BMP180", bmp180_init, bmp180_start_measurement, bmp180_get_measurement },
	{ "BMP280", bmp280_init, bmp280_start_measurement, bmp280_get_measurement },
	{ "DPS310", dps310_init, dps310_start_measurement, dps310_get_measurement },
	{ "MCP9808", mcp9808_init, mcp9808_start_measurement, mcp9808_get_measurement },
	{ "PCT2075", pct2075_init, pct2075_start_measurement, pct2075_get_measurement },
	{ "TMP102", tmp102_init, tmp102_start_measurement, tmp102_get_measurement },
	{ "TMP117", tmp117_init, tmp117_start_measurement, tmp117_get_measurement },
	{ NULL, NULL, NULL, NULL }
};

static bool i2c_bus_active = false;
static i2c_inst_t *i2c_bus = NULL;
static int i2c_temp_sensors = 0;

uint i2c_current_baudrate = I2C_DEFAULT_SPEED / 1000;  // kHz


static int i2c_init_sensor(uint8_t type, uint8_t addr, void **ctx)
{
	uint8_t buf[2];

	if (type < 1 || !ctx || i2c_reserved_address(addr))
		return -1;

	/* Check for a device on given address... */
	if (i2c_read_timeout_us(i2c_bus, addr, buf, 1, false,
					I2C_READ_TIMEOUT(1)) < 0)
			return -2;

	/* Initialize sensor */
	*ctx = i2c_sensor_types[type].init(i2c_bus, addr);

	return (*ctx ? 0 : -3);
}


static int i2c_start_measurement(int sensor_type, void *ctx)
{
	if (sensor_type < 1 || !ctx)
		return -1;

	return i2c_sensor_types[sensor_type].start_measurement(ctx);
}

static int i2c_get_measurement(int sensor_type, void *ctx, float *temp, float *pressure, float *humidity)
{
	if (sensor_type < 1 || !ctx)
		return -1;

	return i2c_sensor_types[sensor_type].get_measurement(ctx, temp, pressure, humidity);
}



inline int32_t twos_complement(uint32_t value, uint8_t bits)
{
	uint32_t mask = ((uint32_t)0xffffffff >> (32 - bits));

	if (value & ((uint32_t)1 << (bits - 1))) {
		/* negative value, set high bits */
		value |= ~mask;
	} else {
		/* positive value, clear high bits */
		value &= mask;
	}

	return (int32_t)value;
}


inline bool i2c_reserved_address(uint8_t addr)
{
	return (addr & 0x78) == 0 || (addr & 0x78) == 0x78;
}


int i2c_read_register_block(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, uint8_t *buf, size_t len)
{
	int res;

	DEBUG_PRINT("args=%p,%02x,%02x,%p,%u\n", i2c, addr, reg, buf, len);
	res = i2c_write_timeout_us(i2c, addr, &reg, 1, true,
				I2C_WRITE_TIMEOUT(1));
	if (res < 1) {
		DEBUG_PRINT("write failed (%d)\n", res);
		return -1;
	}

	res = i2c_read_timeout_us(i2c, addr, buf, len, false,
				I2C_READ_TIMEOUT(len));
	if (res < len) {
		DEBUG_PRINT("read failed (%d)\n", res);
		return -2;
	} else {
#if I2C_DEBUG > 0
		DEBUG_PRINT("read ok: ");
		for(int i = 0; i < len; i++) {
			printf(" %02x", buf[i]);
		}
		printf("\n");
#endif
	}

	return 0;
}


int i2c_read_register_u24(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, uint32_t *val)
{
	uint8_t buf[3];
	int res;

	DEBUG_PRINT("args=%p,%02x,%02x,%p\n", i2c, addr, reg, val);
	res = i2c_write_timeout_us(i2c, addr, &reg, 1, true,
				I2C_WRITE_TIMEOUT(1));
	if (res < 1) {
		DEBUG_PRINT("write failed (%d)\n", res);
		return -1;
	}

	res = i2c_read_timeout_us(i2c, addr, buf, 3, false,
				I2C_READ_TIMEOUT(3));
	if (res < 3) {
		DEBUG_PRINT("read failed (%d)\n", res);
		return -2;
	}

	*val = (buf[0] << 16) | (buf[1] << 8) | buf[2];
	DEBUG_PRINT("read ok: [%02x %02x %02x] %08lx (%lu)\n", buf[0], buf[1], buf[2], *val, *val);

	return 0;
}


int i2c_read_register_u16(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, uint16_t *val)
{
	uint8_t buf[2];
	int res;

	DEBUG_PRINT("args=%p,%02x,%02x,%p\n", i2c, addr, reg, val);
	res = i2c_write_timeout_us(i2c, addr, &reg, 1, true,
				I2C_WRITE_TIMEOUT(1));
	if (res < 1) {
		DEBUG_PRINT("write failed (%d)\n", res);
		return -1;
	}

	res = i2c_read_timeout_us(i2c, addr, buf, 2, false,
				I2C_READ_TIMEOUT(2));
	if (res < 2) {
		DEBUG_PRINT("read failed (%d)\n", res);
		return -2;
	}

	*val = (buf[0] << 8) | buf[1];
	DEBUG_PRINT("read ok: [%02x %02x] %04x (%u)\n", buf[0], buf[1], *val, *val);

	return 0;
}


int i2c_read_register_u8(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, uint8_t *val)
{
	uint8_t buf;
	int res;

	DEBUG_PRINT("args=%p,%02x,%02x,%p\n", i2c, addr, reg, val);
	res = i2c_write_timeout_us(i2c, addr, &reg, 1, true,
				I2C_WRITE_TIMEOUT(1));
	if (res < 1) {
		DEBUG_PRINT("write failed (%d)\n", res);
		return -1;
	}

	res = i2c_read_timeout_us(i2c, addr, &buf, 1, false,
				I2C_READ_TIMEOUT(1));
	if (res < 1) {
		DEBUG_PRINT("read failed (%d)\n", res);
		return -2;
	}

	*val = buf;
	DEBUG_PRINT("read ok: %02x (%u)\n", *val, *val);

	return 0;
}


int i2c_write_register_u16(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, uint16_t val)
{
	uint8_t buf[3];
	int res;

	buf[0] = reg;
	buf[1] = val >> 8;
	buf[2] = val & 0xff;

	DEBUG_PRINT("args=%p,%02x,%02x,%04x (%u)\n", i2c, addr, reg, val, val);

	res = i2c_write_timeout_us(i2c, addr, buf, 3, false,
				I2C_WRITE_TIMEOUT(3));
	if (res < 3) {
		DEBUG_PRINT("write failed (%d)\n", res);
		return -1;
	}

	return 0;
}


int i2c_write_register_u8(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, uint8_t val)
{
	uint8_t buf[2];
	int res;

	buf[0] = reg;
	buf[1] = val;

	DEBUG_PRINT("args=%p,%02x,%02x,%02x (%u)\n", i2c, addr, reg, val, val);

	res = i2c_write_timeout_us(i2c, addr, buf, 2, false,
				I2C_WRITE_TIMEOUT(2));
	if (res < 2) {
		DEBUG_PRINT("write failed (%d)\n", res);
		return -1;
	}

	return 0;
}


uint get_i2c_sensor_type(const char *name)
{
	int type = -1;
	int len;

	if (!name)
		return 0;

	len = strlen(name);

	for (int i = 0; i2c_sensor_types[i].name; i++) {
		if (!strncasecmp(i2c_sensor_types[i].name, name, len)) {
			type = i;
			break;
		}
	}

	return (type > 0 ? type : 0);
}


const char *i2c_sensor_type_str(uint type)
{
	for (int i = 0; i2c_sensor_types[i].name; i++) {
		if (i == type) {
			return i2c_sensor_types[i].name;
		}
	}

	return "NONE";
}


void scan_i2c_bus()
{
	int res;
	uint8_t buf[2];
	int found = 0;

	if (!i2c_bus_active)
		return;

	printf("Scanning I2C Bus... ");

	for (uint addr = 0; addr < 0x80; addr++) {
		if (i2c_reserved_address(addr))
			continue;
		res = i2c_read_timeout_us(i2c_bus, addr, buf, 1, false,
					I2C_READ_TIMEOUT(1));
		if (res < 0)
			continue;
		if (found > 0)
			printf(", ");
		printf("0x%02x", addr);
		found++;
	}

	printf("\nDevice(s) found: %d\n", found);
}



void display_i2c_status()
{
	printf("%d\n", i2c_bus_active ? 1 : 0);
}


void setup_i2c_bus(struct fanpico_config *config)
{
	int res;

	i2c_bus_active = false;
#if SDA_PIN >= 0
	if (!SPI_SHARED || !config->spi_active)
		i2c_bus_active = true;
#endif

	if (!i2c_bus_active) {
		log_msg(LOG_INFO, "I2C Bus disabled");
		return;
	}

	log_msg(LOG_INFO, "Initializing I2C Bus..");

	i2c_bus = (I2C_HW > 1 ? i2c1 : i2c0);
	i2c_current_baudrate = i2c_init(i2c_bus, config->i2c_speed);
	i2c_current_baudrate /= 1000;
	log_msg(LOG_INFO, "I2C Bus initalized at %u kHz", i2c_current_baudrate);
	gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
	gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
	gpio_pull_up(SDA_PIN);
	gpio_pull_up(SCL_PIN);


	/* Scan for I2C Temperature Sensors */

	for (int i = 0; i < VSENSOR_COUNT; i++) {
		struct vsensor_input *v = &config->vsensors[i];
		void *ctx = NULL;

		if (v->mode != VSMODE_I2C)
			continue;
		if (v->i2c_type < 1 || i2c_reserved_address(v->i2c_addr))
			continue;

		res = i2c_init_sensor(v->i2c_type, v->i2c_addr, &ctx);
		if (res) {
			log_msg(LOG_NOTICE, "I2C Device %s (at 0x%02x): failed to initialize",
				i2c_sensor_type_str(v->i2c_type), v->i2c_addr);
			continue;
		}
		config->i2c_context[i] = ctx;
		log_msg(LOG_INFO, "I2C Device %s (at 0x%02x): mapped to vsensor%d",
			i2c_sensor_type_str(v->i2c_type), v->i2c_addr, i + 1);
		i2c_temp_sensors++;
	}
}


int i2c_read_temps(struct fanpico_config *config)
{
	static uint step = 0;
	static uint sensor = 0;
	int wait_time = 0;
	float temp = 0.0;
	float pressure = 0.0;
	float humidity = 0.0;
	int res;

	if (!i2c_bus_active ||  i2c_temp_sensors < 1)
		return -1;

	if (step > 1)
		step = 0;

	if (step == 0) {
		/* Send "Convert Temperature" commands to all sensors */
		log_msg(LOG_DEBUG, "Initiate I2C sensors temperature conversions.");
		for (int i = 0; i < VSENSOR_COUNT; i++) {
			struct vsensor_input *v = &config->vsensors[i];

			if (v->mode != VSMODE_I2C)
				continue;
			if (v->i2c_type < 1 || !config->i2c_context[i])
				continue;
			res = i2c_start_measurement(v->i2c_type, config->i2c_context[i]);
			if (res >= 0) {
				if (res > wait_time)
					wait_time = res;
			} else {
				log_msg(LOG_DEBUG, "vsensor%d: I2C temp conversion fail: %d",
					i + 1, res);
			}
		}
		if (wait_time < 1)
			wait_time = 15000;
		sensor = 0;
		step++;
	}
	else if (step == 1) {
		/* Read temperature measurements one sensor at a time... */
		if (sensor == 0)
			log_msg(LOG_DEBUG, "Initiate I2C sensor measurement readings.");
		int i;
		for (i = sensor; i < VSENSOR_COUNT; i++) {
			struct vsensor_input *v = &config->vsensors[i];

			if (v->mode != VSMODE_I2C)
				continue;
			if (v->i2c_type < 1 || !config->i2c_context[i])
				continue;

			res = i2c_get_measurement(v->i2c_type, config->i2c_context[i], &temp, &pressure, &humidity);
			if (res == 0) {
				if (pressure > 0.0 || humidity > 0.0 ) {
					if (pressure > 0.0)
						pressure /= 100.0;
					log_msg(LOG_DEBUG, "vsensor%d: temp=%0.4fC, pressure=%0.2fhPa, humidity=%0.2f%%",
						i + 1, temp, pressure, humidity);
				} else {
					log_msg(LOG_DEBUG, "vsensor%d: temperature %0.4f C", i + 1, temp);
				}
				mutex_enter_blocking(config_mutex);
				config->vtemp[i] = temp;
				config->vtemp_updated[i] = get_absolute_time();
				mutex_exit(config_mutex);
			} else {
				log_msg(LOG_INFO, "vsensor%d: I2C get temperature failed: %d",
					i + 1, res);
			}
			break;
		}
		sensor = i + 1;
		if (sensor >= VSENSOR_COUNT) {
			wait_time = 15000;
			step++;
			log_msg(LOG_DEBUG, "I2C Temperature measurements complete.");
		} else {
			wait_time = 50;
		}
	}

	return wait_time;
}

