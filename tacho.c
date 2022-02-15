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
#include <string.h>
#include <assert.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "square_wave_gen.h"

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
uint8_t gpio_fan_tacho_map[32];


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

/* Array holding calculated fan tachometer (input) frequencies. */
float fan_tacho_freq[FAN_MAX_COUNT];

PIO pio = pio0;



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
void update_tacho_input_freq()
{
	uint counters[FAN_MAX_COUNT];
	int64_t delta;
	double s;
	uint pulses;
	double f;
	int i;

	/* Read current counter values. */
	for (i = 0; i < FAN_MAX_COUNT; i++) {
		counters[i] = fan_tacho_counters[i];
	}
	absolute_time_t read_time = get_absolute_time();

	/* Calculate new frequency values, if enough time has passed... */
	delta = absolute_time_diff_us(fan_tacho_last_read, read_time);
	if (delta < 1000000)
		return;

	s = delta / 1000000.0;
	for (i = 0; i < FAN_MAX_COUNT; i++) {
		pulses = counters[i] - fan_tacho_counters_last[i];
		f = pulses / s;
		fan_tacho_freq[i] = f;
	}

	/* Save counter values for next time... */
	for (i = 0; i < FAN_MAX_COUNT; i++) {
		fan_tacho_counters_last[i] = counters[i];
	}
	fan_tacho_last_read = read_time;
}


/* Function to initialize inputs for reading tachometer signals.
 */
void setup_tacho_inputs()
{
	int i, pin;

	printf("Setting up Tacho Input pins...\n");

	/* Configure pins and build GPIO to FAN mapping */
	memset(gpio_fan_tacho_map, 0, sizeof(gpio_fan_tacho_map));

	for (i = 0; i < FAN_MAX_COUNT; i++) {
		pin = fan_gpio_tacho_map[i];

		gpio_fan_tacho_map[pin] = 1 + i;
		fan_tacho_counters[i] = 0;
		fan_tacho_counters_last[i] = 0;
		fan_tacho_freq[i] = 0.0;

		gpio_init(pin);
		gpio_set_dir(pin, GPIO_IN);
	}

	/* Enable interrupts on Fan Tacho input pins */
	gpio_set_irq_enabled_with_callback(FAN1_TACHO_READ_PIN,	GPIO_IRQ_EDGE_FALL,
					true, &fan_tacho_read_callback);
	for (i = 1; i < FAN_MAX_COUNT; i++) {
		gpio_set_irq_enabled(fan_gpio_tacho_map[i], GPIO_IRQ_EDGE_RISE, true);
	}
	fan_tacho_last_read = get_absolute_time();
}


/* Function to set output frequency for tachometer output pin.
 */
void set_tacho_output_freq(uint fan, double frequency)
{
	assert(fan < MBFAN_MAX_COUNT);
	square_wave_gen_set_freq(pio, fan, frequency);
}


/* Funcition to initialize tachometer output (generator) pins.
 */
void setup_tacho_outputs()
{
	uint i;

	printf("Setting up Tacho Output pins...\n");

	/* Load square wave generator program to PIO */
	uint pio_program_addr = square_wave_gen_load_program(pio);

	/* Initialize PIO State machines for each tachometer output pin. */
	for (i = 0; i < MBFAN_MAX_COUNT; i++) {
		uint pin = mbfan_gpio_tacho_map[i];
		uint sm = i;
		square_wave_gen_program_init(pio, sm, pio_program_addr, pin);
		square_wave_gen_enabled(pio, sm, true);
	}

}
