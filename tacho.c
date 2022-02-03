/* tacho.c
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
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"

#include "square_wave_gen.pio.h"

#include "fanpico.h"


/* mapping from FAN (index) to GPIO pins */
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

/* mapping from GPIO pin to FAN (index) */
uint8_t gpio_fan_tacho_map[32];

/* tacho pulse counters updated by GPIO interrupt */
volatile uint fan_tacho_counters[FAN_MAX_COUNT];

uint fan_tacho_counters_last[FAN_MAX_COUNT];
absolute_time_t fan_tacho_last_read;
float fan_tacho_freq[FAN_MAX_COUNT];


/* Interrupt handler to keep count on pulses received on fan tachometer pins... */

void __not_in_flash_func(fan_tacho_read_callback)(uint gpio, uint32_t events)
{
	uint fan = gpio_fan_tacho_map[(gpio & 0x1f)];
	if (fan > 0) {
		fan_tacho_counters[fan-1]++;
		//printf("GPIO %d: fan=%d, events=%x, counter=%u\n", gpio, fan, events, fan_tacho_counters[fan-1]);
	}
}


void update_tacho_freq()
{
	uint counters[FAN_MAX_COUNT];
	int64_t delta;
	double s;
	uint pulses;
	double f;
	int i;

	/* read current counter values */
	for (i = 0; i < FAN_MAX_COUNT; i++) {
		counters[i] = fan_tacho_counters[i];
	}
	absolute_time_t read_time = get_absolute_time();

	/* calculate new frequency values, if enough time has passed... */
	delta = absolute_time_diff_us(fan_tacho_last_read, read_time);
	if (delta < 1000000)
		return;

	s = delta / 1000000.0;
	for (i = 0; i < FAN_MAX_COUNT; i++) {
		pulses = counters[i] - fan_tacho_counters_last[i];
		f = pulses / s;
		fan_tacho_freq[i] = f;
		//printf("pulses=%d, delta=%lld (%lf), f=%f\n", pulses, delta, s, f);
	}

	/* save counter values for next time... */
	for (i = 0; i < FAN_MAX_COUNT; i++) {
		fan_tacho_counters_last[i] = counters[i];
	}
	fan_tacho_last_read = read_time;
}


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


void square_wave_gen_program_init(PIO pio, uint sm, uint offset, uint pin)
{
	pio_sm_config config = square_wave_gen_program_get_default_config(offset);

	pio_gpio_init(pio, pin);
	pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);
	sm_config_set_sideset_pins(&config, pin);
	pio_sm_init(pio, sm, offset, &config);
}

void pio_square_wave_gen_set_period(PIO pio, uint sm, uint32_t period)
{
	/* Write 'period' to TX FIFO. State machine copies this into register X */
	pio_sm_put_blocking(pio, sm, period);
}

void setup_tacho_outputs()
{
	PIO pio = pio0;
	uint offset = pio_add_program(pio, &square_wave_gen_program);
	int sm = 0;

	square_wave_gen_program_init(pio, sm, offset, MBFAN1_TACHO_GEN_PIN);
	pio_sm_set_enabled(pio, sm, true);
	pio_square_wave_gen_set_period(pio, sm, 300000);


#if 0
	gpio_init(MBFAN1_TACHO_GEN_PIN);
	gpio_set_dir(MBFAN1_TACHO_GEN_PIN, GPIO_OUT);
	gpio_init(MBFAN2_TACHO_GEN_PIN);
	gpio_set_dir(MBFAN2_TACHO_GEN_PIN, GPIO_OUT);
	gpio_init(MBFAN3_TACHO_GEN_PIN);
	gpio_set_dir(MBFAN3_TACHO_GEN_PIN, GPIO_OUT);
	gpio_init(MBFAN4_TACHO_GEN_PIN);
	gpio_set_dir(MBFAN4_TACHO_GEN_PIN, GPIO_OUT);
#endif
}
