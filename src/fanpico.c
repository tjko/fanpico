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
#include <malloc.h>
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "pico/mutex.h"
#include "pico/multicore.h"
#ifdef LIB_PICO_CYW43_ARCH
#include "pico/cyw43_arch.h"
#endif
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

#if TTL_SERIAL
	stdio_uart_init_full(TTL_SERIAL_UART,
			TTL_SERIAL_SPEED, TX_PIN, RX_PIN);
#endif
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

	read_config(false);
	display_init();
	network_init();

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

	/* Initialize status LED... */
	if (LED_PIN > 0) {
		gpio_init(LED_PIN);
		gpio_set_dir(LED_PIN, GPIO_OUT);
		gpio_put(LED_PIN, 0);
	}
#ifdef LIB_PICO_CYW43_ARCH
	/* On pico_w, LED is connected to the radio GPIO... */
	cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
#endif

	/* Configure PWM pins... */
	setup_pwm_outputs();
	setup_pwm_inputs();

	for (i = 0; i < FAN_COUNT; i++) {
		set_pwm_duty_cycle(i, 0);
	}

	/* Configure Tacho pins... */
	setup_tacho_outputs();
	setup_tacho_inputs();

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


void core1_main()
{
	absolute_time_t t_led = 0;

	debug(1, "core1: started...\n");

	/* Allow core0 to pause this core... */
	multicore_lockout_victim_init();

	while (1) {
		read_tacho_inputs();

		if (time_passed(&t_led, 2000)) {
			debug(2, "core1: tick\n");
		}
	}
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
	absolute_time_t t_network = 0;
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

	multicore_launch_core1(core1_main);

	t_last = get_absolute_time();
	t_display = t_last;

	while (1) {
		change = 0;
		t_now = get_absolute_time();
		delta = absolute_time_diff_us(t_last, t_now);
		t_last = t_now;

		if (delta > max_delta) {
			max_delta = delta;
			debug(2, "%llu: max_loop_time=%lld\n", t_now, max_delta);
		}

		if (time_passed(&t_network, 1)) {
			network_poll();
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
#if LED_PIN > 0
			gpio_put(LED_PIN, led_state);
#endif
#ifdef LIB_PICO_CYW43_ARCH
			cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
#endif
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
			debug(2, "Updating tacho input signals.\n");
			update_tacho_input_freq(st);
		}

		if (change || time_passed(&t_set_outputs, 3000)) {
			debug(2, "Updating output signals.\n");
			update_outputs(st, cfg);
		}

		/* Process any (user) input */
		while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
			if (c == 0xff || c == 0x00)
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
