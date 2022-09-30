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
#include <math.h>
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
float pwm_in_count_rate = 0;

/* Set PMW output signal duty cycle.
 */
void set_pwm_duty_cycle(uint fan, float duty)
{
	uint level, pin;

	assert(fan < FAN_COUNT);
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

	assert(fan < MBFAN_COUNT);
	pin = mbfan_gpio_pwm_map[fan];
	slice_num = pwm_gpio_to_slice_num(pin);

	/* Reset current counter value in PWM slice. */
	pwm_set_enabled(slice_num, false);
	pwm_set_counter(slice_num, 0);

	/* Turn PWM slice (counter) on for short time... */
	absolute_time_t t_start = get_absolute_time();
	pwm_set_enabled(slice_num, true);
	sleep_ms(PWM_IN_SAMPLE_INTERVAL);
	pwm_set_enabled(slice_num, false);
	absolute_time_t t_end = get_absolute_time();

	float max_count = pwm_in_count_rate * ((t_end - t_start) / 1000000.0);

	/* Get counter value and calculate duty cycle. */
	counter = pwm_get_counter(slice_num);
	float duty = counter * 100 / max_count;

	return duty;
}


/* Read multiple PWM signals simultaneously using PWM hardware.
 */
void get_pwm_duty_cycles()
{
	uint slices[MBFAN_COUNT];
	int i;

	/* Reset counters on all PWM slices. */
	for (i=0; i < MBFAN_COUNT; i++) {
		slices[i] = pwm_gpio_to_slice_num(mbfan_gpio_pwm_map[i]);
		pwm_set_enabled(slices[i], false);
		pwm_set_counter(slices[i], 0);
	}

	/* Turn on all PWM slices for short period of time... */
	absolute_time_t t_start = get_absolute_time();
	for (i=0; i < MBFAN_COUNT; i++) {
		pwm_set_enabled(slices[i], true);
	}
	sleep_ms(PWM_IN_SAMPLE_INTERVAL);
	for (i=0; i < MBFAN_COUNT; i++) {
		pwm_set_enabled(slices[i], false);
	}
	absolute_time_t t_end = get_absolute_time();

	float max_count = pwm_in_count_rate * ((t_end - t_start) / 1000000.0);

	if (max_count >= 65535) {
		log_msg(LOG_WARNING, "get_pwm_duty_cycles(): counter overflow: %f (%llu)",
			max_count, (t_end - t_start));
		return;
	}

	/* Calculate duty cycles based on measurements. */
	for (i=0; i < MBFAN_COUNT; i++) {
		uint16_t counter = pwm_get_counter(slices[i]);
		float duty = counter * 100 / max_count;
		mbfan_pwm_duty[i] = duty;
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

	log_msg(LOG_NOTICE, "Initializing PWM outputs...");
	log_msg(LOG_NOTICE, "PWM Frequency: %0.2f kHz", pwm_freq / 1000.0);

	pwm_out_top = (sys_clock / pwm_freq / 2) - 1;  /* for phase-correct PWM signal */

	pwm_config_set_clkdiv(&config, 1);
	pwm_config_set_phase_correct(&config, 1);
	pwm_config_set_wrap(&config, pwm_out_top);

	/* Configure PWM outputs */

	for (i = 0; i < FAN_COUNT; i=i+2) {
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

	log_msg(LOG_NOTICE, "Initializing PWM Inputs...");

	pwm_config_set_clkdiv_mode(&config, PWM_DIV_B_HIGH);
	pwm_config_set_clkdiv(&config, PWM_IN_CLOCK_DIVIDER);

	pwm_in_count_rate = clock_get_hz(clk_sys) / PWM_IN_CLOCK_DIVIDER;

	for (i = 0; i < MBFAN_COUNT; i++) {
		uint pin = mbfan_gpio_pwm_map[i];
		mbfan_pwm_duty[i] = 0.0;

		slice_num = pwm_gpio_to_slice_num(pin);
		/* reading PWM signal must be done on B channel... */
		assert(pwm_gpio_to_channel(pin) == PWM_CHAN_B);
		pwm_init(slice_num, &config, false);
		gpio_set_function(pin, GPIO_FUNC_PWM);
	}

}


double pwm_map(struct pwm_map *map, double val)
{
	int i;
	double newval;

	// value is equal or smaller than first map point
	if (val <= map->pwm[0][0])
		return map->pwm[0][1];

	// find the map points that the value falls in between of...
	i = 1;
	while(i < map->points && map->pwm[i][0] < val)
		i++;

	// value is larger or equal than last map point
	if (val >= map->pwm[i][0])
		return map->pwm[i][1];

	// calculate mapping using the map points left (i-i) and right (i) of the value...
	double a = (double)(map->pwm[i][1] - map->pwm[i-1][1]) / (double)(map->pwm[i][0] - map->pwm[i-1][0]);
	newval = map->pwm[i-1][1] + a * (val - map->pwm[i-1][0]);

	return newval;
}


double calculate_pwm_duty(struct fanpico_state *state, struct fanpico_config *config, int i)
{
	struct fan_output *fan;
	double val = 0;

	fan = &config->fans[i];

	/* get source value  */
	switch (fan->s_type) {
	case PWM_FIXED:
		val = fan->s_id;
		break;
	case PWM_MB:
		val = state->mbfan_duty[fan->s_id];
		break;
	case PWM_SENSOR:
		val = sensor_get_duty(&config->sensors[fan->s_id], state->temp[fan->s_id]);
		//printf("temp duty: %fC --> %lf\n", state->temp[fan->s_id], val);
		break;
	case PWM_FAN:
		val = state->fan_duty[fan->s_id];
		break;
	}

	/* apply mapping */
	val = pwm_map(&fan->map, val);

	/* apply coefficient */
	val *= fan->pwm_coefficient;

	/* final step to enforce min/max limits for output */
	if (val < fan->min_pwm) val = fan->min_pwm;
	if (val > fan->max_pwm) val = fan->max_pwm;

	return val;
}


