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
#include "pico/unique_id.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"

#include "fanpico.h"


static struct fanpico_state system_state;



void setup()
{
	pico_unique_board_id_t board_id;
	int i;

	stdio_usb_init();

	// Wait a while for USB Serial to connect...
	i = 0;
	while (i++ < 10) {
		if (stdio_usb_connected())
			break;
		sleep_ms(250);
	}

	printf("\n\n\n");
	if (watchdog_enable_caused_reboot()) {
		printf("[Rebooted by watchdog]\n\n");
	}

	/* Run "SYStem:VERsion" command... */
	cmd_version(NULL, NULL, 0, NULL);
	printf("Hardware Model: FANPICO-%s\n", FANPICO_MODEL);
	printf("         Board: %s\n", PICO_BOARD);
	printf("           MCU: %s @ %0.0fMHz\n",
		rp2040_model_str(),
		clock_get_hz(clk_sys) / 1000000.0);
	printf(" Serial Number: ");
	pico_get_unique_board_id(&board_id);
	for (i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++)
		printf("%02x", board_id.id[i]);
	printf("\n\n");

	read_config();
	display_init();

	/* Enable ADC */
	printf("Initialize ADC...\n");
	adc_init();
	adc_set_temp_sensor_enabled(true);
	if (SENSOR1_READ_PIN > 0)
		adc_gpio_init(SENSOR1_READ_PIN);
	if (SENSOR2_READ_PIN > 0)
		adc_gpio_init(SENSOR2_READ_PIN);

	/* Setup GPIO pins... */
	printf("Initialize GPIO...\n");

	if (LED_PIN > 0) {
		gpio_init(LED_PIN);
		gpio_set_dir(LED_PIN, GPIO_OUT);
		gpio_put(LED_PIN, 0);
	}

	/* Configure PWM pins... */
	setup_pwm_outputs();
	setup_pwm_inputs();

	for (i = 0; i < FAN_COUNT; i++) {
		set_pwm_duty_cycle(i, 0);
	}

	/* Configure Tacho pins... */
	setup_tacho_outputs();
	setup_tacho_inputs();

	sleep_ms(1000);
	printf("Initialization complete.\n\n");
}


void clear_state(struct fanpico_state *s)
{
	int i;

	for (i = 0; i < MBFAN_MAX_COUNT; i++) {
		s->mbfan_duty[i] = 0.0;
		s->mbfan_freq[i] = 0.0;
	}
	for (i = 0; i < FAN_MAX_COUNT; i++) {
		s->fan_duty[i] = 0.0;
		s->fan_freq[i] = 0.0;
	}
	for (i = 0; i < SENSOR_MAX_COUNT; i++) {
		s->temp[i] = 0.0;
	}
}


int check_for_change(double oldval, double newval, double treshold)
{
	double delta = fabs(oldval - newval);

	if (delta >= treshold || newval < oldval)
		return 1;

	return 0;
}


void update_outputs(struct fanpico_state *state, struct fanpico_config *config)
{
	int i;
	float new;

	/* update fan PWM signals */
	for (i = 0; i < FAN_COUNT; i++) {
		new = calculate_pwm_duty(state, config, i);
		if (check_for_change(state->fan_duty[i], new, 0.1)) {
			debug(1, "fan%d: Set output PWM %.1f%% --> %.1f%%\n",
				i+1,
				state->fan_duty[i],
				new);
			state->fan_duty[i] = new;
			set_pwm_duty_cycle(i, new);
		}
	}

	/* update mb tacho signals */
	for (i = 0; i < MBFAN_COUNT; i++) {
		new = calculate_tacho_freq(state, config, i);
		if (check_for_change(state->mbfan_freq[i], new, 0.1)) {
			debug(1, "mbfan%d: Set output Tacho %.2fHz --> %.2fHz\n",
				i+1,
				state->mbfan_freq[i],
				new);
			state->mbfan_freq[i] = new;
			set_tacho_output_freq(i, new);
		}
	}
}


int time_passed(absolute_time_t *t, uint32_t us)
{
	absolute_time_t t_now = get_absolute_time();

	if (t == NULL)
		return -1;

	if (*t == 0 || delayed_by_ms(*t, us) < t_now) {
		*t = t_now;
		return 1;
	}

	return 0;
}


int main()
{
	struct fanpico_state *st = &system_state;
	absolute_time_t t_now;
	absolute_time_t t_last = 0;
	absolute_time_t t_poll_pwm = 0;
	absolute_time_t t_poll_tacho = 0;
	absolute_time_t t_led = 0;
	absolute_time_t t_temp = 0;
	absolute_time_t t_set_outputs = 0;
	absolute_time_t t_display = 0;
	uint8_t led_state = 0;
	int64_t delta;
	int64_t max_delta = 0;
	int c, change;
	char input_buf[1024];
	int i_ptr = 0;


	set_binary_info();
	clear_state(st);

	/* Initialize MCU and other hardware... */
	if (get_debug_level() >= 2)
		print_mallinfo();
	setup();
	if (get_debug_level() >= 2)
		print_mallinfo();

	t_last = get_absolute_time();

	while (1) {
		change = 0;
		t_now = get_absolute_time();
		delta = absolute_time_diff_us(t_last, t_now);
		t_last = t_now;

		if (delta > max_delta) {
			max_delta = delta;
			debug(2, "%llu: max_loop_time=%lld\n", t_now, max_delta);
		}

		/* Toggle LED every 1000ms */
		if (time_passed(&t_led, 1000)) {
			if (cfg->led_mode == 0) {
				/* Slow blinking */
				led_state = (led_state > 0 ? 0 : 1);
			} else if (cfg->led_mode == 1) {
				/* Always on */
				led_state = 1;
			} else {
				/* Always off */
				led_state = 0;
			}
			gpio_put(LED_PIN, led_state);
		}

		/* Update display every 2000ms */
		if (time_passed(&t_display, 2000)) {
			display_status(st, cfg);
		}

		/* Read PWM input signals (duty cycle) periodically */
		if (time_passed(&t_poll_pwm, 1500)) {
			int i;
			float new_duty;

			get_pwm_duty_cycles();
			for (i = 0; i < MBFAN_COUNT; i++) {
				new_duty = roundf(mbfan_pwm_duty[i]);
				if (check_for_change(st->mbfan_duty[i], new_duty, 0.5)) {
					debug(1, "mbfan%d: duty cycle change %.1f --> %.1f\n",
						i+1,
						st->mbfan_duty[i],
						new_duty);
					st->mbfan_duty[i] = new_duty;
					t_set_outputs = 0;
					change++;
				}
			}
		}

		/* Read temperature sensors periodically */
		if (time_passed(&t_temp, 10000)) {
			int i;
			float temp;

			for (i = 0; i < SENSOR_COUNT; i++) {
				temp = get_temperature(i);
				if (check_for_change(st->temp[i], temp, 0.5)) {
					debug(1, "sensor%d: Temperature change %.1fC --> %.1fC\n",
						i+1,
						st->temp[i],
						temp);
					st->temp[i] = temp;
					change++;
				}
			}
		}

		/* Calculate frequencies from input tachometer signals peridocially */
		if (time_passed(&t_poll_tacho, 2000)) {
			int i;
			float new_freq;

			update_tacho_input_freq();
			for (i = 0; i < FAN_COUNT; i++) {
				new_freq = roundf(fan_tacho_freq[i]*10)/10.0;
				if (check_for_change(st->fan_freq[i], new_freq, 1.0)) {
					debug(1, "fan%d: tacho freq change %.1f --> %.1f (%f)\n",
						i+1,
						st->fan_freq[i],
						new_freq,
						fan_tacho_freq[i]);
					st->fan_freq[i] = new_freq;
					change++;
				}
			}
		}

		if (change || time_passed(&t_set_outputs, 5000)) {
			debug(2, "Updating output signals.\n");
			update_outputs(st, cfg);
		}

		/* Process any (user) input */
		while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
			if (c == 0xff)
				continue;
			if (c == 0x7f || c == 0x08) {
				if (i_ptr > 0) i_ptr--;
				if (cfg->local_echo) printf("\b \b");
				continue;
			}
			if (c == 10 || c == 13 || i_ptr >= sizeof(input_buf)) {
				if (cfg->local_echo) printf("\r\n");
				input_buf[i_ptr] = 0;
				if (i_ptr > 0) {
					process_command(st, cfg, input_buf);
					i_ptr = 0;
				}
				continue;
			}
			input_buf[i_ptr++] = c;
			if (cfg->local_echo) printf("%c", c);
		}

	}
}


/* eof :-) */
