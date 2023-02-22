/* tacho.c
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
#include <string.h>
#include <assert.h>
#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "square_wave_gen.h"
#include "pulse_len.h"
#include "fanpico.h"

/*
 * Functions for generating (emulating) fan tachometer output using PIO
 * and functions for reading tachometer singals from fans to calculate
 * signal frequency and fan RPM.
 */


/* Map of input pins to read fan tachometer signals.
 */
uint8_t fan_gpio_tacho_map[FAN_MAX_COUNT] = {
	FAN1_TACHO_READ_PIN,
	FAN2_TACHO_READ_PIN,
	FAN3_TACHO_READ_PIN,
	FAN4_TACHO_READ_PIN,
	FAN5_TACHO_READ_PIN,
	FAN6_TACHO_READ_PIN,
	FAN7_TACHO_READ_PIN,
	FAN8_TACHO_READ_PIN,
};

/* Reverse map of input pin numbers to fan numbers.
 */
uint8_t gpio_fan_tacho_map[MAX_GPIO_PINS];


/* Map of output pins that output/generate tachometer signal using PIO.
 */
uint8_t mbfan_gpio_tacho_map[MBFAN_MAX_COUNT] = {
	MBFAN1_TACHO_GEN_PIN,
	MBFAN2_TACHO_GEN_PIN,
	MBFAN3_TACHO_GEN_PIN,
	MBFAN4_TACHO_GEN_PIN,
};


/* Tachometer pulse counters updated by GPIO interrupt */
volatile uint fan_tacho_counters[FAN_MAX_COUNT];

uint fan_tacho_counters_last[FAN_MAX_COUNT];
absolute_time_t fan_tacho_last_read;



auto_init_mutex(tacho_mutex);

/* Array holding calculated fan tachometer (input) frequencies.
 *
 * Note, access to this array must use tacho_mutex as core1
 * writes to this array.
 */

float fan_tacho_freq[FAN_MAX_COUNT];


PIO pio = pio0;

static uint8_t queue[FAN_MAX_COUNT] = { 0, 0, 0, 0, 0, 0 , 0, 0 };
static uint8_t queue_pos[2] = { 0, 0 };


/* Get next fan in the given queue. */
int next_in_queue(uint8_t q)
{
	assert(q < 2);

	if (queue_pos[q] >= FAN_MAX_COUNT) {
		queue_pos[q] = 0;
		return -1;
	}

	while (queue[queue_pos[q]] != q) {
		queue_pos[q]++;
		if (queue_pos[q] >= FAN_MAX_COUNT) {
			queue_pos[q] = 0;
			return -1;
		}
	}
	return queue_pos[q]++;
}


/* Function to select active multiplexer port. */
void multiplexer_select(uint8_t port)
{
	gpio_put(FAN_TACHO_READ_S0_PIN, port & 0x01);
	gpio_put(FAN_TACHO_READ_S1_PIN, port & 0x02);
	gpio_put(FAN_TACHO_READ_S2_PIN, port & 0x04);
}


/* Interrupt handler to keep count on pulses received on fan tachometer pins...
 */
void __time_critical_func(fan_tacho_read_callback)(uint gpio, uint32_t events)
{
	uint fan = gpio_fan_tacho_map[(gpio & 0x1f)];
	if (fan > 0) {
		fan_tacho_counters[fan-1]++;
	}
}


/* Function to update tachometer frequencies in fan_tacho_freq[]
 */
void read_tacho_inputs()
#if TACHO_READ_MULTIPLEX == 0
{
	uint counters[FAN_COUNT];
	int64_t delta;
	double s;
	uint pulses;
	double f;
	int i;

	/* Read current counter values. */
	for (i = 0; i < FAN_COUNT; i++) {
		counters[i] = fan_tacho_counters[i];
	}
	absolute_time_t read_time = get_absolute_time();

	/* Calculate new frequency values, if enough time has passed... */
	delta = absolute_time_diff_us(fan_tacho_last_read, read_time);
	if (delta < 1000000)
		return;

	s = delta / 1000000.0;
	mutex_enter_blocking(&tacho_mutex);
	for (i = 0; i < FAN_COUNT; i++) {
		pulses = counters[i] - fan_tacho_counters_last[i];
		f = pulses / s;
		fan_tacho_freq[i] = f;
	}
	mutex_exit(&tacho_mutex);

	/* Save counter values for next time... */
	for (i = 0; i < FAN_COUNT; i++) {
		fan_tacho_counters_last[i] = counters[i];
	}
	fan_tacho_last_read = read_time;
}
#else
{
	static int q = 0;
	int i;
	uint64_t t;
	double f;


	/* Pick next fan to 'measure' from queues... */
	i = next_in_queue(q);
	if (i < 0) {
                /* End of queue reached, go to next queue. */
		if (++q >= 2)
			q = 0;
		return;
	}

	/* process only one entry from second queue at a time */
	if (q == 1) {
		log_msg(LOG_DEBUG, "queue2: fan%d: read", i+1);
		q = 0;
	}

	/* Switch multiplexer to the fan we want to measure from... */
	multiplexer_select(fan_gpio_tacho_map[i]);
	sleep_us(100);

	// Measure pulse up to 600ms (down to 50 RPM)...
	t = pulse_measure(FAN_TACHO_READ_PIN, 1, 0, 600);
	if (t > 0) {
		/* Fan is spinning, make sure fan is in the first queue... */
		if (queue[i] != 0)
			queue[i] = 0;

		f = 1 / (t / 1000000.0);
	}
	else {
		/* Fan not spinning, put this fan into second queue */
		if (queue[i] != 1)
			queue[i] = 1;

		f = 0;
	}

	log_msg(LOG_DEBUG + 1, "fan%d: pulse len=%llu", i+1, t);

	mutex_enter_blocking(&tacho_mutex);
	fan_tacho_freq[i] = f;
	mutex_exit(&tacho_mutex);
}
#endif


/* Function to calculate tachometer frequencies.
 */
void update_tacho_input_freq(struct fanpico_state *st)
{
	int i;
	float new_freq;
	float freq[FAN_COUNT];

	mutex_enter_blocking(&tacho_mutex);
	for (i = 0; i < FAN_COUNT; i++) {
		freq[i] = fan_tacho_freq[i];
	}
	mutex_exit(&tacho_mutex);

	for (i = 0; i < FAN_COUNT; i++) {
		new_freq = roundf(freq[i]*10)/10.0;
		if (check_for_change(st->fan_freq[i], new_freq, 1.0)) {
			log_msg(LOG_INFO, "fan%d: tacho freq change %.1f --> %.1f",
				i+1,
				st->fan_freq[i],
				new_freq);
			st->fan_freq[i] = new_freq;
		}
	}
}


/* Function to initialize inputs for reading tachometer signals.
 */
void setup_tacho_inputs()
{
	int i, pin;

	log_msg(LOG_NOTICE, "Setting up Tacho Input pins...");

	/* Configure pins and build GPIO to FAN mapping */
	memset(gpio_fan_tacho_map, 0, sizeof(gpio_fan_tacho_map));

	mutex_enter_blocking(&tacho_mutex);
	for (i = 0; i < FAN_COUNT; i++) {
		fan_tacho_counters[i] = 0;
		fan_tacho_counters_last[i] = 0;
		fan_tacho_freq[i] = 0.0;
		pin = fan_gpio_tacho_map[i];
		gpio_fan_tacho_map[pin] = 1 + i;
#if TACHO_READ_MULTIPLEX == 0
		gpio_init(pin);
		gpio_set_dir(pin, GPIO_IN);
#endif
	}
	mutex_exit(&tacho_mutex);

#if TACHO_READ_MULTIPLEX > 0
	// Setup multiplexer pins */
	gpio_init(FAN_TACHO_READ_PIN);
	gpio_set_dir(FAN_TACHO_READ_PIN, GPIO_IN);
	gpio_init(FAN_TACHO_READ_S0_PIN);
	gpio_init(FAN_TACHO_READ_S1_PIN);
	gpio_init(FAN_TACHO_READ_S2_PIN);
	gpio_set_dir(FAN_TACHO_READ_S0_PIN, GPIO_OUT);
	gpio_set_dir(FAN_TACHO_READ_S1_PIN, GPIO_OUT);
	gpio_set_dir(FAN_TACHO_READ_S2_PIN, GPIO_OUT);
	multiplexer_select(0);
#else
	/* Enable interrupts on Fan Tacho input pins */
	gpio_set_irq_enabled_with_callback(FAN1_TACHO_READ_PIN,	GPIO_IRQ_EDGE_RISE,
					true, &fan_tacho_read_callback);
	for (i = 1; i < FAN_COUNT; i++) {
		gpio_set_irq_enabled(fan_gpio_tacho_map[i], GPIO_IRQ_EDGE_RISE, true);
	}
#endif
	fan_tacho_last_read = get_absolute_time();
}


/* Function to set output frequency for tachometer output pin.
 */
void set_tacho_output_freq(uint fan, double frequency)
{
	assert(fan < MBFAN_COUNT);
	square_wave_gen_set_freq(pio, fan, frequency);
}


/* Funcition to initialize tachometer output (generator) pins.
 */
void setup_tacho_outputs()
{
	uint i;

	log_msg(LOG_NOTICE, "Setting up Tacho Output pins...");

	/* Load square wave generator program to PIO */
	uint pio_program_addr = square_wave_gen_load_program(pio);

	/* Initialize PIO State machines for each tachometer output pin. */
	for (i = 0; i < MBFAN_COUNT; i++) {
		uint pin = mbfan_gpio_tacho_map[i];
		uint sm = i;
		square_wave_gen_program_init(pio, sm, pio_program_addr, pin);
		square_wave_gen_set_period(pio, sm, 0);
		square_wave_gen_enabled(pio, sm, true);
	}

}


double tacho_map(struct tacho_map *map, double val)
{
	int i;
	double newval;

	/* Value is equal or smaller than first map point */
	if (val <= map->tacho[0][0])
		return map->tacho[0][1];

	/* Find the map points that the value falls in between of... */
	i = 1;
	while(i < map->points && map->tacho[i][0] < val)
		i++;

	/* Value is larger or equal than last map point */
	if (val >= map->tacho[i][0])
		return map->tacho[i][1];

	/* Calculate mapping using the map points left (i-i) and right (i) of the value... */
	double a = (double)(map->tacho[i][1] - map->tacho[i-1][1]) / (double)(map->tacho[i][0] - map->tacho[i-1][0]);
	newval = map->tacho[i-1][1] + a * (val - map->tacho[i-1][0]);

	return newval;
}


double calculate_tacho_freq(struct fanpico_state *state, struct fanpico_config *config, int i)
{
	struct mb_input *mbfan;
	double val = 0;

	mbfan = &config->mbfans[i];

	switch (mbfan->s_type) {
	case TACHO_FIXED:
		val = mbfan->s_id;
		break;
	case TACHO_FAN:
		val = state->fan_freq[mbfan->s_id] * 60.0 / config->fans[mbfan->s_id].rpm_factor;
		break;
	}


	/* apply mapping */
	val = tacho_map(&mbfan->map, val);

	/* apply coefficient */
	val *= mbfan->rpm_coefficient;

	if (val < mbfan->min_rpm) val = mbfan->min_rpm;
	if (val > mbfan->max_rpm) val = mbfan->max_rpm;

	/* convert RPM to frequency */
	val = val / 60.0 * mbfan->rpm_factor;

	return val;
}


