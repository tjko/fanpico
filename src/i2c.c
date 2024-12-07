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
#include "pico_sensor_lib.h"

#include "fanpico.h"


static bool i2c_bus_active = false;
static i2c_inst_t *i2c_bus = NULL;
static int i2c_temp_sensors = 0;



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
		res = i2c_read_timeout_us(i2c_bus, addr, buf, 1, false,	10000);
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
	uint baudrate;

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
	baudrate = i2c_init(i2c_bus, config->i2c_speed);
	baudrate /= 1000;
	i2c_sensor_baudrate(baudrate);
	log_msg(LOG_INFO, "I2C Bus initialized at %u kHz", baudrate);
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
		if (v->i2c_type < 1)
			continue;

		res = i2c_init_sensor(v->i2c_type, i2c_bus, v->i2c_addr, &ctx);
		if (res) {
			log_msg(LOG_NOTICE, "I2C Device %s (at 0x%02x): failed to initialize: %d",
				i2c_sensor_type_str(v->i2c_type), v->i2c_addr, res);
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
			res = i2c_start_measurement(config->i2c_context[i]);
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

			res = i2c_read_measurement(config->i2c_context[i], &temp, &pressure, &humidity);
			if (res == 0) {
				if (pressure >= 0.0 || humidity >= 0.0 ) {
					log_msg(LOG_DEBUG, "vsensor%d: temp=%0.4fC, pressure=%0.2fhPa, humidity=%0.2f%%",
						i + 1, temp, pressure, humidity);
				} else {
					log_msg(LOG_DEBUG, "vsensor%d: temperature %0.4f C", i + 1, temp);
				}
				mutex_enter_blocking(config_mutex);
				config->vtemp[i] = temp;
				config->vpressure[i] = pressure;
				config->vhumidity[i] = humidity;
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
			wait_time = 10000;
			step++;
			log_msg(LOG_DEBUG, "I2C Temperature measurements complete.");
		} else {
			wait_time = 50;
		}
	}

	return wait_time;
}

