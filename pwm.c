/* pwm.c
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
   along with FanPico. If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#include "fanpico.h"

#define PWM_IN_CLOCK_DIVIDER 100
#define PWM_IN_SAMPLE_INTERVAL 10 /* milliseconds */

uint pwm_out_top = 0;
float pwm_in_max_count = 0;


void set_pwm_duty_cycle(uint pin, float duty)
{
	uint level = 0;

	if (duty >= 100.0) {
		level = pwm_out_top + 1;
	} else if (duty > 0.0) {
		level = (duty * (pwm_out_top + 1) / 100);
	}
	//printf("set_pwm_duty_cycle(pin=%u, duty=%f): level=%u\n", pin, duty, level);
	pwm_set_gpio_level(pin, level);
}


float get_pwm_duty_cycle(uint pin)
{
	uint16_t counter;
	uint slice_num = pwm_gpio_to_slice_num(pin);

	pwm_set_enabled(slice_num, false);
	pwm_set_counter(slice_num, 0);
	pwm_set_enabled(slice_num, true);
	sleep_ms(PWM_IN_SAMPLE_INTERVAL);
	pwm_set_enabled(slice_num, false);
	counter = pwm_get_counter(slice_num);

	float duty = counter * 100 / pwm_in_max_count;
	//printf("get_pwm_duty_cycle(pin=%d): duty=%f (counter=%u)\n", pin, duty, counter);

	return duty;
}


void get_pwm_duty_cycles(uint *pins, uint count, float *duty)
{
	uint slices[8];
	int i;

	if (count > 8) return;

	/* reset counters on all PWM slices */
	for (i=0; i < count; i++) {
		slices[i] = pwm_gpio_to_slice_num(pins[i]);
		pwm_set_enabled(slices[i], false);
		pwm_set_counter(slices[i], 0);
	}

	for (i=0; i < count; i++) {
		pwm_set_enabled(slices[i], true);
	}
	sleep_ms(PWM_IN_SAMPLE_INTERVAL);
	for (i=0; i < count; i++) {
		pwm_set_enabled(slices[i], false);
	}

	for (i=0; i < count; i++) {
		duty[i] = pwm_get_counter(slices[i]) * 100 / pwm_in_max_count;
	}
}




void setup_pwm_outputs()
{
	uint32_t sys_clock = clock_get_hz(clk_sys);
	pwm_config config = pwm_get_default_config();
	uint pwm_freq = 25000;
	uint slice_num;

	printf("Initializing PWM outputs...\n");
	printf("System Clock Speed: %0.2f MHz\n", sys_clock / 1000000.0);

	printf("PWM Frequency: %0.2f kHz\n", pwm_freq / 1000.0);
	pwm_out_top = (sys_clock / pwm_freq / 2) - 1;  /* for phase-correct PWM signal */
	printf("PWM Top: %u\n", pwm_out_top);

	pwm_config_set_clkdiv(&config, 1);
	pwm_config_set_phase_correct(&config, 1);
	pwm_config_set_wrap(&config, pwm_out_top);


	/* Configure PWM outputs */

	gpio_set_function(FAN1_PWM_GEN_PIN, GPIO_FUNC_PWM);
	gpio_set_function(FAN2_PWM_GEN_PIN, GPIO_FUNC_PWM);
	slice_num = pwm_gpio_to_slice_num(FAN1_PWM_GEN_PIN);
	assert(slice_num == pwm_gpio_to_slice_num(FAN2_PWM_GEN_PIN));
	pwm_init(slice_num, &config, true);

	gpio_set_function(FAN3_PWM_GEN_PIN, GPIO_FUNC_PWM);
	gpio_set_function(FAN4_PWM_GEN_PIN, GPIO_FUNC_PWM);
	slice_num = pwm_gpio_to_slice_num(FAN3_PWM_GEN_PIN);
	assert(slice_num == pwm_gpio_to_slice_num(FAN4_PWM_GEN_PIN));
	pwm_init(slice_num, &config, true);

	gpio_set_function(FAN5_PWM_GEN_PIN, GPIO_FUNC_PWM);
	gpio_set_function(FAN6_PWM_GEN_PIN, GPIO_FUNC_PWM);
	slice_num = pwm_gpio_to_slice_num(FAN5_PWM_GEN_PIN);
	assert(slice_num == pwm_gpio_to_slice_num(FAN6_PWM_GEN_PIN));
	pwm_init(slice_num, &config, true);

	gpio_set_function(FAN7_PWM_GEN_PIN, GPIO_FUNC_PWM);
	gpio_set_function(FAN8_PWM_GEN_PIN, GPIO_FUNC_PWM);
	slice_num = pwm_gpio_to_slice_num(FAN7_PWM_GEN_PIN);
	assert(slice_num == pwm_gpio_to_slice_num(FAN8_PWM_GEN_PIN));
	pwm_init(slice_num, &config, true);

}


void setup_pwm_inputs()
{
	pwm_config config = pwm_get_default_config();
	uint slice_num;

	printf("Initializing PWM Inputs...\n");

	pwm_config_set_clkdiv_mode(&config, PWM_DIV_B_HIGH);
	pwm_config_set_clkdiv(&config, PWM_IN_CLOCK_DIVIDER);

	float counting_rate = clock_get_hz(clk_sys) / PWM_IN_CLOCK_DIVIDER;
	pwm_in_max_count = counting_rate * (PWM_IN_SAMPLE_INTERVAL / 1000.0);


	slice_num = pwm_gpio_to_slice_num(MBFAN1_PWM_READ_PIN);
	assert(pwm_gpio_to_channel(MBFAN1_PWM_READ_PIN) == PWM_CHAN_B);
	pwm_init(slice_num, &config, false);
	gpio_set_function(MBFAN1_PWM_READ_PIN, GPIO_FUNC_PWM);

	slice_num = pwm_gpio_to_slice_num(MBFAN2_PWM_READ_PIN);
	assert(pwm_gpio_to_channel(MBFAN2_PWM_READ_PIN) == PWM_CHAN_B);
	pwm_init(slice_num, &config, false);
	gpio_set_function(MBFAN2_PWM_READ_PIN, GPIO_FUNC_PWM);

	slice_num = pwm_gpio_to_slice_num(MBFAN3_PWM_READ_PIN);
	assert(pwm_gpio_to_channel(MBFAN3_PWM_READ_PIN) == PWM_CHAN_B);
	pwm_init(slice_num, &config, false);
	gpio_set_function(MBFAN3_PWM_READ_PIN, GPIO_FUNC_PWM);

	slice_num = pwm_gpio_to_slice_num(MBFAN4_PWM_READ_PIN);
	assert(pwm_gpio_to_channel(MBFAN4_PWM_READ_PIN) == PWM_CHAN_B);
	pwm_init(slice_num, &config, false);
	gpio_set_function(MBFAN4_PWM_READ_PIN, GPIO_FUNC_PWM);



}
