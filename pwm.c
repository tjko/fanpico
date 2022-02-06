/* pwm.c
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
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#include "fanpico.h"

#define PWM_IN_CLOCK_DIVIDER 100
#define PWM_IN_SAMPLE_INTERVAL 10 /* milliseconds */


/*
 * Functions for generating (emulating) fan PWM control signal and
 * reading (motherboard) PWM signal for controlling fans.
 * Pico PWM hardware is used for both.
 */


/* Map of fan PWM signal output pins.
   Every two pins must be the A and B pins of same PWM slice.
 */
uint8_t fan_gpio_pwm_map[FAN_MAX_COUNT] = {
	FAN1_PWM_GEN_PIN,
	FAN2_PWM_GEN_PIN,
	FAN3_PWM_GEN_PIN,
	FAN4_PWM_GEN_PIN,
	FAN5_PWM_GEN_PIN,
	FAN6_PWM_GEN_PIN,
	FAN7_PWM_GEN_PIN,
	FAN8_PWM_GEN_PIN,
};

/* Map of motherboard PWM input pins.
   Every pin must be the B pin of a PWM slice.
 */
uint8_t mbfan_gpio_pwm_map[MBFAN_MAX_COUNT] = {
	MBFAN1_PWM_READ_PIN,
	MBFAN2_PWM_READ_PIN,
	MBFAN3_PWM_READ_PIN,
	MBFAN4_PWM_READ_PIN,
};

/* Measured duty cycles from (motherboard) fan connectors.  */
float mbfan_pwm_duty[MBFAN_MAX_COUNT];

uint pwm_out_top = 0;
float pwm_in_max_count = 0;


/* Set PMW output signal duty cycle.
 */
void set_pwm_duty_cycle(uint fan, float duty)
{
	uint level, pin;

	assert(fan < FAN_MAX_COUNT);
	pin = fan_gpio_pwm_map[fan];
	if (duty >= 100.0) {
		level = pwm_out_top + 1;
	} else if (duty > 0.0) {
		level = (duty * (pwm_out_top + 1) / 100);
	} else {
		level = 0;
	}
	pwm_set_gpio_level(pin, level);
}


/* Measure duty cycle of input PWM signal.
 */
float get_pwm_duty_cycle(uint fan)
{
	uint16_t counter;
	uint slice_num, pin;

	assert(fan < MBFAN_MAX_COUNT);
	pin = mbfan_gpio_pwm_map[fan];
	slice_num = pwm_gpio_to_slice_num(pin);

	/* Reset current counter value in PWM slice. */
	pwm_set_enabled(slice_num, false);
	pwm_set_counter(slice_num, 0);

	/* Turn PWM slice (counter) on for short time... */
	pwm_set_enabled(slice_num, true);
	sleep_ms(PWM_IN_SAMPLE_INTERVAL);
	pwm_set_enabled(slice_num, false);

	/* Get counter value and calculate duty cycle. */
	counter = pwm_get_counter(slice_num);
	float duty = counter * 100 / pwm_in_max_count;

	return duty;
}


/* Read multiple PWM signals simultaneously using PWM hardware.
 */
void get_pwm_duty_cycles()
{
	uint slices[MBFAN_MAX_COUNT];
	int i;

	/* Reset counters on all PWM slices. */
	for (i=0; i < MBFAN_MAX_COUNT; i++) {
		slices[i] = pwm_gpio_to_slice_num(mbfan_gpio_pwm_map[i]);
		pwm_set_enabled(slices[i], false);
		pwm_set_counter(slices[i], 0);
	}

	/* Turn on all PWM slices for short period of time... */
	for (i=0; i < MBFAN_MAX_COUNT; i++) {
		pwm_set_enabled(slices[i], true);
	}
	sleep_ms(PWM_IN_SAMPLE_INTERVAL);
	for (i=0; i < MBFAN_MAX_COUNT; i++) {
		pwm_set_enabled(slices[i], false);
	}

	/* Calculate duty cycles based on measurements. */
	for (i=0; i < MBFAN_MAX_COUNT; i++) {
		mbfan_pwm_duty[i] = pwm_get_counter(slices[i]) * 100 / pwm_in_max_count;
	}
}


/* Initialize PWM hardware to generate 25kHz PWM signal on output pins.
 */
void setup_pwm_outputs()
{
	uint32_t sys_clock = clock_get_hz(clk_sys);
	pwm_config config = pwm_get_default_config();
	uint pwm_freq = 25000;
	uint slice_num;
	int i;

	printf("Initializing PWM outputs...\n");
	printf("System Clock Speed: %0.2f MHz\n", sys_clock / 1000000.0);

	printf("PWM Frequency: %0.2f kHz\n", pwm_freq / 1000.0);
	pwm_out_top = (sys_clock / pwm_freq / 2) - 1;  /* for phase-correct PWM signal */
	printf("PWM Top: %u\n", pwm_out_top);

	pwm_config_set_clkdiv(&config, 1);
	pwm_config_set_phase_correct(&config, 1);
	pwm_config_set_wrap(&config, pwm_out_top);

	/* Configure PWM outputs */

	for (i = 0; i < FAN_MAX_COUNT; i=i+2) {
		uint pin1 = fan_gpio_pwm_map[i];
		uint pin2 = fan_gpio_pwm_map[i + 1];

		gpio_set_function(pin1, GPIO_FUNC_PWM);
		gpio_set_function(pin2, GPIO_FUNC_PWM);
		slice_num = pwm_gpio_to_slice_num(pin1);
		/* two consecutive pins must belong to same PWM slice... */
		assert(slice_num == pwm_gpio_to_slice_num(pin2));
		pwm_init(slice_num, &config, true);
	}

}


/* Initialize PWM hardware for measuring input signal duty cycle on input pins.
 */
void setup_pwm_inputs()
{
	pwm_config config = pwm_get_default_config();
	uint slice_num;
	int i;

	printf("Initializing PWM Inputs...\n");

	pwm_config_set_clkdiv_mode(&config, PWM_DIV_B_HIGH);
	pwm_config_set_clkdiv(&config, PWM_IN_CLOCK_DIVIDER);

	float counting_rate = clock_get_hz(clk_sys) / PWM_IN_CLOCK_DIVIDER;
	pwm_in_max_count = counting_rate * (PWM_IN_SAMPLE_INTERVAL / 1000.0);

	for (i = 0; i < MBFAN_MAX_COUNT; i++) {
		uint pin = mbfan_gpio_pwm_map[i];
		mbfan_pwm_duty[i] = 0.0;

		slice_num = pwm_gpio_to_slice_num(pin);
		/* reading PWM signal must be done on B channel... */
		assert(pwm_gpio_to_channel(pin) == PWM_CHAN_B);
		pwm_init(slice_num, &config, false);
		gpio_set_function(pin, GPIO_FUNC_PWM);
	}

}
