/* fanpico.c
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




float get_pico_temp()
{
	uint16_t raw;
	float temp, volt;
	float cal_value = 0.0;

	adc_select_input(4);
	raw = adc_read();
	volt = raw * 3.25 / (1 << 12);
	temp = 27 - ((volt - 0.706) / 0.001721) + cal_value;

	//printf("get_pico_temp(): raw=%u, volt=%f, temp=%f\n", raw, volt, temp);
	return roundf(temp);
}

void set_binary_info()
{
	bi_decl(bi_program_description("FanPico - Smart PWM Fan Controller"));
	bi_decl(bi_program_version_string(VERSION " ("__DATE__")"));
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
	int i;

	set_binary_info();
	stdio_init_all();

	sleep_ms(500);
	printf("\n\n\n");
	printf("FanPico v%s\n", VERSION);
	printf("Copyright (C) 2021-2022 Timo Kokkonen <tjko@iki.fi>\n");
	printf("Build date: %s\n", __DATE__);

	read_config();

	/* Enable ADC */
	printf("Initialize ADC...\n");
	adc_init();
	adc_set_temp_sensor_enabled(true);
	adc_select_input(4);

	/* Setup GPIO pins... */
	printf("Initialize GPIO...\n");

	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	gpio_put(LED_PIN, 1);

	/* Configure PWM pins... */
	setup_pwm_outputs();
	setup_pwm_inputs();

	for (i = 0; i < FAN_MAX_COUNT; i++) {
		set_pwm_duty_cycle(i, 10 + i * 10);
	}

	/* Configure Tacho pins... */
	setup_tacho_outputs();
	setup_tacho_inputs();

	set_tacho_output_freq(0,42);
	set_tacho_output_freq(1,43);
	set_tacho_output_freq(2,44);
	set_tacho_output_freq(3,45);


	printf("Initialization complete.\n\n");
}


int main()
{
	/* Initialize MCU and other hardware... */
	setup();

	uint count = 0;
	uint8_t buf[32];




	uint checksum = 0xffffffff;

	while (1) {
		gpio_put(LED_PIN, 0);
		sleep_ms(250);
		gpio_put(LED_PIN, 1);
		checksum = xcrc32(buf, sizeof(buf), checksum);
		printf("Hello World %x\n", checksum);
#if 0
		float d = get_pwm_duty_cycle(0);
		printf("fan1 duty=%f\n", d);
		float d2 = get_pwm_duty_cycle(1);
		printf("fan2 duty=%f\n", d2);
#endif
		get_pwm_duty_cycles();
		printf("mbpwm: fan1=%f,fan2=%f,fan3=%f,fan4=%f\n",
			mbfan_pwm_duty[0],
			mbfan_pwm_duty[1],
			mbfan_pwm_duty[2],
			mbfan_pwm_duty[3]);
#if 0
		float temp = get_pico_temp();
		printf("temperature=%fC\n", temp);
		printf("tacho: fan1=%u, fan2=%u, fan3=%u, fan4=%u\n",
			fan_tacho_counters[0],
			fan_tacho_counters[1],
			fan_tacho_counters[2],
			fan_tacho_counters[3] );
#endif

		if (count % 1 == 0) {
			update_tacho_input_freq();
			printf("tacho in (Hz): f1=%0.2f, f2=%0.2f, f3=%0.2f, f4=%0.2f, f5=%0.2f, f6=%0.2f, f7=%0.2f, f8=%0.2f\n",
				fan_tacho_freq[0],
				fan_tacho_freq[1],
				fan_tacho_freq[2],
				fan_tacho_freq[3],
				fan_tacho_freq[4],
				fan_tacho_freq[5],
				fan_tacho_freq[6],
				fan_tacho_freq[7]
				);
		}
		count++;

		sleep_ms(1000);
	}
}

