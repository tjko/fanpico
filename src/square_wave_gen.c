/* square_wave_gen.c
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
#include "hardware/pio.h"
#include "hardware/clocks.h"

#include "square_wave_gen.h"

// Include the assembled PIO program
#include "square_wave_gen.pio.h"


/*
 * Functions for PIO based Adjustable Square Wave Generator.
 */


/* Function for loading Square Wave generator program into a PIO.
 */
uint square_wave_gen_load_program(PIO pio)
{
	return pio_add_program(pio, &square_wave_gen_program);
}


/* Function to initialize PIO state machine to run Square Wave Generator
 * program.
 */
void square_wave_gen_program_init(PIO pio, uint sm, uint offset, uint pin)
{
	pio_sm_config config = square_wave_gen_program_get_default_config(offset);

	pio_gpio_init(pio, pin);
	pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);
	sm_config_set_sideset_pins(&config, pin);
	sm_config_set_clkdiv(&config, 1.0);
	pio_sm_init(pio, sm, offset, &config);
}


/* Function to enable/disable a Square Wave generator.
 */
void square_wave_gen_enabled(PIO pio, uint sm, bool enabled)
{
	pio_sm_set_enabled(pio, sm, enabled);
}


/* Function to set output signal 'period' of a Square Wave generator.
 */
void square_wave_gen_set_period(PIO pio, uint sm, uint32_t period)
{
	/* Write 'period' to TX FIFO. State machine copies this into register X */
	pio_sm_put_blocking(pio, sm, period);
}


/* Function to set output signal frequency of a Square Wave generator.
 */
void square_wave_gen_set_freq(PIO pio, uint sm, double freq)
{
	uint32_t sys_clock = clock_get_hz(clk_sys);
	uint32_t period;

	if (freq > 0) {
		// Calculate 'period' needed to produce requested frequency square wave...
		period = sys_clock / (freq * 2);
		if (period > 5)
			period -= 5;
	} else {
		// no output if frequency <= 0 ...
		period = 0;
	}

	square_wave_gen_set_period(pio, sm, period);
}


