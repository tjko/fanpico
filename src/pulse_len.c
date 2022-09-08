/* pulse_len.c
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


#include "pico/stdlib.h"
#include "hardware/gpio.h"

/*
 * Functions for measuring pulses on GPIO pins.
 */


/* pulse_measure - measure pulse length or interval from a GPIO
 *
 * This function does not use interrupts. On a RP2040 this was observed
 * to achieve about +/- 1 microsecond accuracy, based on quick test with
 * a signal generator. Function works down to about 5 microsecond pulse
 * lengths.
 *
 *     pin: GPIO input pin
 *    type: true=measure HIGH pulse, false = measure LOW pulse
 *    mode: true=measure pulse length, false = measure time between two pulses
 * timeout: timeout in ms, return 0 if no pulse seen before timeout
 *
 */
uint64_t pulse_measure(int pin, bool type, bool mode, uint32_t timeout_ms)
{
	absolute_time_t start = get_absolute_time();
	absolute_time_t now, pulse_start;
	uint64_t timeout = timeout_ms * 1000;
	int state = 0;
	bool in;


	do {
		in = gpio_get(pin);
		now = get_absolute_time();

		if (state == 0 && in == !type) {
			/* wait until previous pulse is done... */
			state = 1;
		}
		else if (state == 1 && in == type) {
			/* pulse start detected */
			state = 2;
			pulse_start = now;
		}
		else if (state == 2 && in == !type) {
			/* pulse end detected */
			if (mode) {
				return absolute_time_diff_us(pulse_start, now);
			} else {
				state = 3;
			}
		}
		else if (state == 3 && in == type) {
			/* next pulse start detected */
			return absolute_time_diff_us(pulse_start, now);
		}
	} while (absolute_time_diff_us(start, now) <= timeout);

	return 0;
}

/* pulseIn -  Arduino style wrapper for pulse_measure
 */
uint64_t pulseIn(int gpio, int value, uint32_t timeout_ms)
{
	return pulse_measure(gpio, (value ? true : false), true, timeout_ms);
}



volatile uint pulse_pin = 0;
volatile uint32_t pulse_counter = 0;
absolute_time_t pulse_last_read;
uint32_t pulse_events;


/* Interrupt handler to measure pulse interval
 */
void __time_critical_func(pulse_measure_callback)(uint gpio, uint32_t events)
{
	if (gpio == pulse_pin)
		pulse_counter++;
}

void pulse_setup_interrupt(uint gpio, uint32_t events)
{
	pulse_pin = gpio;
	pulse_events = events;
	pulse_counter = 0;
	pulse_last_read = get_absolute_time();

	gpio_set_irq_enabled_with_callback(gpio, events, true,
					&pulse_measure_callback);
}

void pulse_disable_interrupt()
{
	gpio_set_irq_enabled(pulse_pin, pulse_events, false);
}

uint64_t pulse_interval()
{
	uint32_t pulses;
	absolute_time_t read_time, delta;
	uint64_t i;

	/* Read current counter value. */
	pulses = pulse_counter;
	read_time = get_absolute_time();

	if (pulses < 1)
		return 0;

	pulse_counter = 0;

	/* Calculate average interval between pulses. */
	delta = absolute_time_diff_us(pulse_last_read, read_time);
	i = delta / pulses;

	pulse_last_read = read_time;

	return i;
}
