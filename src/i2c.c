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



static const i2c_sensor_entry_t i2c_sensor_types[] = {
	{ "NONE", NULL, NULL, NULL }, /* this needs to be first so that valid sensors have index > 0 */
	{ "TMP117", tmp117_init, tmp117_start_measurement, tmp117_get_measurement },
	{ NULL, NULL, NULL, NULL }
};

static bool i2c_bus_active = false;
static i2c_inst_t *i2c_bus = NULL;
static int i2c_temp_sensors = 0;



static int i2c_init_sensor(uint8_t type, uint8_t addr, void **ctx)
{
	uint8_t buf[2];

	if (type < 1 || !ctx || i2c_reserved_address(addr))
		return -1;

	/* Check for a device on given address... */
	if (i2c_read_blocking(i2c_bus, addr, buf, 1, false) < 0)
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

static int i2c_get_measurement(int sensor_type, void *ctx, float *temp)
{
	if (sensor_type < 1 || !ctx)
		return -1;

	return i2c_sensor_types[sensor_type].get_measurement(ctx, temp);
}



bool i2c_reserved_address(uint8_t addr)
{
	return (addr & 0x78) == 0 || (addr & 0x78) == 0x78;
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
		res = i2c_read_blocking(i2c_bus, addr, buf, 1, false);
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
	int count = 0;
	uint speed;
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

	log_msg(LOG_INFO, "Initializing I2C Bus...",
		config->i2c_speed / 1000);

	i2c_bus = (I2C_HW > 1 ? i2c1 : i2c0);
	speed = i2c_init(i2c_bus, config->i2c_speed);
	log_msg(LOG_INFO, "I2C Bus initalized at %u kHz", speed / 1000);
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
			log_msg(LOG_NOTICE, "Failed to initialize I2C sensor: %s,%02x",
				i2c_sensor_type_str(v->i2c_type), v->i2c_addr);
		}
		config->i2c_context[i] = ctx;
		log_msg(LOG_NOTICE,"I2C Device%d: %s (at 0x%02x)", ++count,
			i2c_sensor_type_str(v->i2c_type), v->i2c_addr);
		i2c_temp_sensors++;
	}
}


int i2c_read_temps(struct fanpico_config *config)
{
	static uint step = 0;
	static uint sensor = 0;
	int wait_time = 0;
	float temp = 0.0;
	int res;

	if (!i2c_bus_active ||  i2c_temp_sensors < 1)
		return -1;

	if (step > 1)
		step = 0;

	if (step == 0) {
		/* Send "Convert Temperature" commands to all sensors */
		log_msg(LOG_DEBUG, "Initiate I2C sensors temperature conversion.");
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
		int i;
		for (i = sensor; i < VSENSOR_COUNT; i++) {
			struct vsensor_input *v = &config->vsensors[i];

			if (v->mode != VSMODE_I2C)
				continue;
			if (v->i2c_type < 1 || !config->i2c_context[i])
				continue;

			res = i2c_get_measurement(v->i2c_type, config->i2c_context[i], &temp);
			if (res == 0) {
				log_msg(LOG_INFO, "vsensor%d: temperature %0.4f C", i + 1, temp);
				mutex_enter_blocking(config_mutex);
				config->vtemp[i] = temp;
				config->vtemp_updated[i] = get_absolute_time();
				mutex_exit(config_mutex);
			} else {
				log_msg(LOG_DEBUG, "vsensor%d: I2C get temperature failed: %d",
					i + 1, res);
			}
			break;
		}
		sensor = i + 1;
		if (sensor >= VSENSOR_COUNT) {
			wait_time = 15000;
			step++;
		} else {
			wait_time = 100;
		}
	}

	return wait_time;
}

