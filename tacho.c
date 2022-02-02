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

uint fan_tacho_counters[FAN_MAX_COUNT];


/* Interrupt handler to keep count on pulses received on fan tachometer pins... */

void __not_in_flash_func(fan_tacho_read_callback)(uint gpio, uint32_t events)
{
	uint fan = gpio_fan_tacho_map[(gpio & 0x1f)];
	if (fan > 0) {
		fan_tacho_counters[fan-1]++;
		//printf("GPIO %d: fan=%d, events=%x, counter=%u\n", gpio, fan, events, fan_tacho_counters[fan-1]);
	}
}


void setup_tacho_inputs()
{
	int i;

	printf("Setting up Tacho Input pins...\n");

	/* Build GPIO to FAN mapping and reset counters */
	memset(gpio_fan_tacho_map, 0, sizeof(gpio_fan_tacho_map));
	for (i = 0; i < FAN_MAX_COUNT; i++) {
		gpio_fan_tacho_map[fan_gpio_tacho_map[i]] = 1 + i;
		fan_tacho_counters[i] = 0;
	}

	/* Configure pins as inputs... */
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

	/* Enable interrupts on Fan Tacho input pins */
	gpio_set_irq_enabled_with_callback(FAN1_TACHO_READ_PIN,	GPIO_IRQ_EDGE_RISE,
					true, &fan_tacho_read_callback);
	for (i = 1; i < FAN_MAX_COUNT; i++) {
		gpio_set_irq_enabled(fan_gpio_tacho_map[i], GPIO_IRQ_EDGE_RISE, true);
	}

}


void setup_tacho_outputs()
{
	gpio_init(MBFAN1_TACHO_GEN_PIN);
	gpio_set_dir(MBFAN1_TACHO_GEN_PIN, GPIO_OUT);
	gpio_init(MBFAN2_TACHO_GEN_PIN);
	gpio_set_dir(MBFAN2_TACHO_GEN_PIN, GPIO_OUT);
	gpio_init(MBFAN3_TACHO_GEN_PIN);
	gpio_set_dir(MBFAN3_TACHO_GEN_PIN, GPIO_OUT);
	gpio_init(MBFAN4_TACHO_GEN_PIN);
	gpio_set_dir(MBFAN4_TACHO_GEN_PIN, GPIO_OUT);

}
