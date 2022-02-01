/* fanpico.c
   Copyright (C) 2021-2022 Timo Kokkonen <tjko@iki.fi>

   SPDX-License-Identifier: GPL-3.0-or-later

   This file is part of FanPico.

   FanPico is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Foobar is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Foobar. If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"

#include "fanpico.h"

#define VERSION "1.0beta"


const uint LED_PIN = 25;


/* Pins for Fan Signals */

const uint FAN1_TACHO_READ_PIN = 2;
const uint FAN2_TACHO_READ_PIN = 3;
const uint FAN3_TACHO_READ_PIN = 20;
const uint FAN4_TACHO_READ_PIN = 21;
const uint FAN5_TACHO_READ_PIN = 22;
const uint FAN6_TACHO_READ_PIN = 26;
const uint FAN7_TACHO_READ_PIN = 27;
const uint FAN8_TACHO_READ_PIN = 28;

const uint FAN1_PWM_GEN_PIN = 4;  /* PWM2A */
const uint FAN2_PWM_GEN_PIN = 5;  /* PWM2B */
const uint FAN3_PWM_GEN_PIN = 6;  /* PWM3A */
const uint FAN4_PWM_GEN_PIN = 7;  /* PWM3B */
const uint FAN5_PWM_GEN_PIN = 8;  /* PWM4A */
const uint FAN6_PWM_GEN_PIN = 9;  /* PWM4B */
const uint FAN7_PWM_GEN_PIN = 10; /* PWM5A */
const uint FAN8_PWM_GEN_PIN = 11; /* PWM5B */


/* Pins for Motherboar Fan Connector Signals */

const uint MBFAN1_TACHO_GEN_PIN = 12;
const uint MBFAN2_TACHO_GEN_PIN = 14;
const uint MBFAN3_TACHO_GEN_PIN = 16;
const uint MBFAN4_TACHO_GEN_PIN = 18;

const uint MBFAN1_PWM_READ_PIN = 13; /* PWM6B */
const uint MBFAN2_PWM_READ_PIN = 15; /* PWM7B */
const uint MBFAN3_PWM_READ_PIN = 17; /* PWM0B */
const uint MBFAN4_PWM_READ_PIN = 19; /* PWM1B */


float get_pico_temp()
{
	uint16_t raw;
	float temp, volt;
	float cal_value = 0.0;

	adc_select_input(4);
	raw = adc_read();
	volt = raw * 3.25 / (1 << 12);
	temp = 27 - ((volt - 0.706) / 0.001721) + cal_value;

	printf("get_pico_temp(): raw=%u, volt=%f, temp=%f\n", raw, volt, temp);
	return roundf(temp);
}

void set_binary_info()
{
	bi_decl(bi_program_description("FanPico - Smart PWM Fan Controller"));
	bi_decl(bi_program_version_string(VERSION));
	bi_decl(bi_program_url("https://github.com/tjko/fanpico/"));

	bi_decl(bi_1pin_with_name(LED_PIN, "On-board LED"));

	bi_decl(bi_1pin_with_name(FAN1_TACHO_READ_PIN, "Fan1 tacho signal (input)"));
	bi_decl(bi_1pin_with_name(FAN2_TACHO_READ_PIN, "Fan2 tacho signal (input)"));
	bi_decl(bi_1pin_with_name(FAN3_TACHO_READ_PIN, "Fan3 tacho signal (input)"));
	bi_decl(bi_1pin_with_name(FAN4_TACHO_READ_PIN, "Fan4 tacho signal (input)"));
	bi_decl(bi_1pin_with_name(FAN5_TACHO_READ_PIN, "Fan5 tacho signal (input)"));
	bi_decl(bi_1pin_with_name(FAN6_TACHO_READ_PIN, "Fan6 tacho signal (input)"));
	bi_decl(bi_1pin_with_name(FAN7_TACHO_READ_PIN, "Fan7 tacho signal (input)"));
	bi_decl(bi_1pin_with_name(FAN8_TACHO_READ_PIN, "Fan8 tacho signal (input)"));

	bi_decl(bi_1pin_with_name(FAN1_PWM_GEN_PIN, "Fan1 PWM signal (output)"));
	bi_decl(bi_1pin_with_name(FAN2_PWM_GEN_PIN, "Fan2 PWM signal (output)"));
	bi_decl(bi_1pin_with_name(FAN3_PWM_GEN_PIN, "Fan3 PWM signal (output)"));
	bi_decl(bi_1pin_with_name(FAN4_PWM_GEN_PIN, "Fan4 PWM signal (output)"));
	bi_decl(bi_1pin_with_name(FAN5_PWM_GEN_PIN, "Fan5 PWM signal (output)"));
	bi_decl(bi_1pin_with_name(FAN6_PWM_GEN_PIN, "Fan6 PWM signal (output)"));
	bi_decl(bi_1pin_with_name(FAN7_PWM_GEN_PIN, "Fan7 PWM signal (output)"));
	bi_decl(bi_1pin_with_name(FAN8_PWM_GEN_PIN, "Fan8 PWM signal (output)"));

	bi_decl(bi_1pin_with_name(MBFAN1_TACHO_GEN_PIN, "MB Fan1 tacho signal (output)"));
	bi_decl(bi_1pin_with_name(MBFAN2_TACHO_GEN_PIN, "MB Fan2 tacho signal (output)"));
	bi_decl(bi_1pin_with_name(MBFAN3_TACHO_GEN_PIN, "MB Fan3 tacho signal (output)"));
	bi_decl(bi_1pin_with_name(MBFAN4_TACHO_GEN_PIN, "MB Fan4 tacho signal (output)"));

	bi_decl(bi_1pin_with_name(MBFAN1_PWM_READ_PIN, "MB Fan1 PWM signal (input)"));
	bi_decl(bi_1pin_with_name(MBFAN2_PWM_READ_PIN, "MB Fan2 PWM signal (input)"));
	bi_decl(bi_1pin_with_name(MBFAN3_PWM_READ_PIN, "MB Fan3 PWM signal (input)"));
	bi_decl(bi_1pin_with_name(MBFAN4_PWM_READ_PIN, "MB Fan4 PWM signal (input)"));
}


void setup()
{
	int res;

	set_binary_info();
	stdio_init_all();

	sleep_ms(500);
	printf("\n\n\n");
	printf("FanPico v%s\n", VERSION);
	printf("Copyright (C) 2021-2022 Timo Kokkonen <tjko@iki.fi>\n");
	printf("Build date: %s\n", __DATE__);


	/* Enable ADC */
	printf("Initialize ADC...\n");
	adc_init();
	adc_set_temp_sensor_enabled(true);
	adc_select_input(4);

	/* Setup GPIO pins */

	printf("Initialize GPIO...\n");
	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	gpio_put(LED_PIN, 1);

	gpio_init(FAN1_TACHO_READ_PIN);
	gpio_set_dir(FAN1_TACHO_READ_PIN, GPIO_IN);
	gpio_init(FAN2_TACHO_READ_PIN);
	gpio_set_dir(FAN2_TACHO_READ_PIN, GPIO_IN);
	gpio_init(FAN3_TACHO_READ_PIN);
	gpio_set_dir(FAN3_TACHO_READ_PIN, GPIO_IN);
	gpio_init(FAN4_TACHO_READ_PIN);
	gpio_set_dir(FAN4_TACHO_READ_PIN, GPIO_IN);
	gpio_init(FAN5_TACHO_READ_PIN);
	gpio_set_dir(FAN5_TACHO_READ_PIN, GPIO_IN);
	gpio_init(FAN6_TACHO_READ_PIN);
	gpio_set_dir(FAN6_TACHO_READ_PIN, GPIO_IN);
	gpio_init(FAN7_TACHO_READ_PIN);
	gpio_set_dir(FAN7_TACHO_READ_PIN, GPIO_IN);
	gpio_init(FAN8_TACHO_READ_PIN);
	gpio_set_dir(FAN8_TACHO_READ_PIN, GPIO_IN);

	gpio_init(MBFAN1_TACHO_GEN_PIN);
	gpio_set_dir(MBFAN1_TACHO_GEN_PIN, GPIO_OUT);
	gpio_init(MBFAN2_TACHO_GEN_PIN);
	gpio_set_dir(MBFAN2_TACHO_GEN_PIN, GPIO_OUT);
	gpio_init(MBFAN3_TACHO_GEN_PIN);
	gpio_set_dir(MBFAN3_TACHO_GEN_PIN, GPIO_OUT);
	gpio_init(MBFAN4_TACHO_GEN_PIN);
	gpio_set_dir(MBFAN4_TACHO_GEN_PIN, GPIO_OUT);


	/* Configure PWM Hardware */
	setup_pwm_outputs();
	setup_pwm_inputs();

	set_pwm_duty_cycle(FAN1_PWM_GEN_PIN, 50.0);
	set_pwm_duty_cycle(FAN2_PWM_GEN_PIN, 25.0);


	read_config();

	printf("Initialization complete.\n\n");
}


int main()
{
	/* Initialize MCU and other hardware... */
	setup();

	uint8_t buf[32];
	float duty[4];
	uint pins[4] = {
		MBFAN1_PWM_READ_PIN,
		MBFAN2_PWM_READ_PIN,
		MBFAN3_PWM_READ_PIN,
		MBFAN4_PWM_READ_PIN
	};

	uint checksum = 0xffffffff;

	while (1) {
		gpio_put(LED_PIN, 0);
		sleep_ms(250);
		gpio_put(LED_PIN, 1);
		checksum = xcrc32(buf, sizeof(buf), checksum);
		printf("Hello World %x\n", checksum);
#if 0
		float d = get_pwm_duty_cycle(MBFAN1_PWM_READ_PIN);
		printf("fan1 duty=%f\n", d);
		float d2 = get_pwm_duty_cycle(MBFAN2_PWM_READ_PIN);
		printf("fan2 duty=%f\n", d2);
#endif
		get_pwm_duty_cycles(pins,4,duty);
		printf("all: fan1=%f,fan2=%f,fan3=%f,fan4=%f\n", duty[0], duty[1], duty[2], duty[3]);
		float temp = get_pico_temp();
		printf("temperature=%fC\n", temp);
		sleep_ms(1000);
	}
}

