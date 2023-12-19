/* fanpico.c
   Copyright (C) 2021-2023 Timo Kokkonen <tjko@iki.fi>

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
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "pico/multicore.h"
#include "pico/util/datetime.h"
#ifdef LIB_PICO_CYW43_ARCH
#include "pico/cyw43_arch.h"
#endif
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/rtc.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "hardware/vreg.h"

#include "fanpico.h"

static struct fanpico_state core1_state;
static struct fanpico_config core1_config;

static struct fanpico_state transfer_state;

static struct fanpico_state system_state;
const struct fanpico_state *fanpico_state = &system_state;

auto_init_mutex(state_mutex_inst);
mutex_t *state_mutex = &state_mutex_inst;
bool rebooted_by_watchdog = false;


void boot_reason()
{
	printf("     CHIP_RESET: %08lx\n", vreg_and_chip_reset_hw->chip_reset);
	printf("WATCHDOG_REASON: %08lx\n", watchdog_hw->reason);
}

void setup()
{
	int i = 0;

	rtc_init();

	stdio_usb_init();
	/* Wait a while for USB Serial to connect... */
	while (i++ < 40) {
		if (stdio_usb_connected())
			break;
		sleep_ms(50);
	}


	read_config(false);

#if TTL_SERIAL
	/* Initialize serial console if configured... */
	if(cfg->serial_active && !cfg->spi_active) {
		stdio_uart_init_full(TTL_SERIAL_UART,
				TTL_SERIAL_SPEED, TX_PIN, RX_PIN);
	}
#endif
	printf("\n\n");
#ifndef NDEBUG
	boot_reason();
#endif
	if (watchdog_enable_caused_reboot()) {
		printf("[Rebooted by watchdog]\n");
		rebooted_by_watchdog = true;
	}
	printf("\n");

	/* Run "SYStem:VERsion" command... */
	cmd_version(NULL, NULL, 0, NULL);
	printf("Hardware Model: FANPICO-%s\n", FANPICO_MODEL);
	printf("         Board: %s\n", PICO_BOARD);
	printf("           MCU: %s @ %0.0fMHz\n",
		rp2040_model_str(),
		clock_get_hz(clk_sys) / 1000000.0);
	printf(" Serial Number: %s\n\n", pico_serial_str());

	display_init();
	network_init(&system_state);

	/* Enable ADC */
	log_msg(LOG_NOTICE, "Initialize ADC...");
	adc_init();
	adc_set_temp_sensor_enabled(true);
	if (SENSOR1_READ_PIN > 0)
		adc_gpio_init(SENSOR1_READ_PIN);
	if (SENSOR2_READ_PIN > 0)
		adc_gpio_init(SENSOR2_READ_PIN);

	/* Setup GPIO pins... */
	log_msg(LOG_NOTICE, "Initialize GPIO...");

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

	/* Setup timezone */
	if (strlen(cfg->timezone) > 1) {
		log_msg(LOG_NOTICE, "Set Timezone: %s", cfg->timezone);
		setenv("TZ", cfg->timezone, 1);
		tzset();
	}

	log_msg(LOG_NOTICE, "System initialization complete.");
}


void clear_state(struct fanpico_state *s)
{
	int i;

	for (i = 0; i < MBFAN_MAX_COUNT; i++) {
		s->mbfan_duty[i] = 0.0;
		s->mbfan_duty_prev[i] = 0.0;
		s->mbfan_freq[i] = 0.0;
		s->mbfan_freq_prev[i] = 0.0;
	}
	for (i = 0; i < FAN_MAX_COUNT; i++) {
		s->fan_duty[i] = 0.0;
		s->fan_duty_prev[i] = 0.0;
		s->fan_freq[i] = 0.0;
		s->fan_freq_prev[i] = 0.0;
	}
	for (i = 0; i < SENSOR_MAX_COUNT; i++) {
		s->temp[i] = 0.0;
		s->temp_prev[i] = 0.0;
	}
	for (i = 0; i < VSENSOR_MAX_COUNT; i++) {
		s->vtemp[i] = 0.0;
		s->vtemp_prev[i] = 0.0;
		s->vtemp_updated[i] = 0;
	}
}


/* update_system_state()
 *  System state gets updated by core1 periodically (using state_mutex)
 *  to intermediate buffer transfer_state.
 *  This function updates system state from that buffer.
 */

void update_system_state()
{
	mutex_enter_blocking(state_mutex);
	memcpy(&system_state, &transfer_state, sizeof(system_state));
	mutex_exit(state_mutex);
}


void update_outputs(struct fanpico_state *state, const struct fanpico_config *config)
{
	int i;

	/* Update fan PWM signals */
	for (i = 0; i < FAN_COUNT; i++) {
		state->fan_duty[i] = calculate_pwm_duty(state, config, i);
		if (check_for_change(state->fan_duty_prev[i], state->fan_duty[i], 1.0)) {
			log_msg(LOG_INFO, "fan%d: Set output PWM %.1f%% --> %.1f%%",
				i+1,
				state->fan_duty_prev[i],
				state->fan_duty[i]);
			state->fan_duty_prev[i] = state->fan_duty[i];
			set_pwm_duty_cycle(i, state->fan_duty[i]);
		}
	}

	/* Update mb tacho signals */
	for (i = 0; i < MBFAN_COUNT; i++) {
		state->mbfan_freq[i] = calculate_tacho_freq(state, config, i);
		if (check_for_change(state->mbfan_freq_prev[i], state->mbfan_freq[i], 1.0)) {
			log_msg(LOG_INFO, "mbfan%d: Set output Tacho %.2fHz --> %.2fHz",
				i+1,
				state->mbfan_freq_prev[i],
				state->mbfan_freq[i]);
			state->mbfan_freq_prev[i] = state->mbfan_freq[i];
			set_tacho_output_freq(i, state->mbfan_freq[i]);
		}
	}
}


void core1_main()
{
	struct fanpico_config *config = &core1_config;
	struct fanpico_state *state = &core1_state;
	absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(t_temp, 0);
	absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(t_tacho, 0);
	absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(t_poll_pwm, 0);
	absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(t_set_outputs, 0);
	absolute_time_t t_last, t_now, t_config, t_state;
	int64_t max_delta = 0;
	int64_t delta;


	log_msg(LOG_INFO, "core1: started...");

	/* Allow core0 to pause this core... */
	multicore_lockout_victim_init();

	setup_tacho_input_interrupts();

	t_state = t_config = t_last = get_absolute_time();

	while (1) {
		t_now = get_absolute_time();
		delta = absolute_time_diff_us(t_last, t_now);
		t_last = t_now;

		if (delta > max_delta || delta > 1000000) {
			max_delta = delta;
			log_msg(LOG_INFO, "core1: max_loop_time=%lld", max_delta);
		}

		/* Tachometer inputs from Fans */
		read_tacho_inputs();
		if (time_passed(&t_tacho, 1000)) {
			/* Calculate frequencies from input tachometer signals peridocially */
			log_msg(LOG_DEBUG, "Updating tacho input signals.");
			update_tacho_input_freq(state);
		}

		/* PWM input signals (duty cycles) from "motherboard". */
		get_pwm_duty_cycles(config);
		if (time_passed(&t_poll_pwm, 200)) {
			log_msg(LOG_DEBUG, "Read PWM inputs");
			for (int i = 0; i < MBFAN_COUNT; i++) {
				state->mbfan_duty[i] = roundf(mbfan_pwm_duty[i]);
				if (check_for_change(state->mbfan_duty_prev[i], state->mbfan_duty[i], 1.0)) {
					log_msg(LOG_INFO, "mbfan%d: Input PWM change %.1f%% --> %.1f%%",
						i+1,
						state->mbfan_duty_prev[i],
						state->mbfan_duty[i]);
					state->mbfan_duty_prev[i] = state->mbfan_duty[i];
				}
			}
		}

		/* Read temperature sensors periodically */
		if (time_passed(&t_temp, 2000)) {
			log_msg(LOG_DEBUG, "Read temperature sensors");
			for (int i = 0; i < SENSOR_COUNT; i++) {
				state->temp[i] = get_temperature(i, config);
				if (check_for_change(state->temp_prev[i], state->temp[i], 0.5)) {
					log_msg(LOG_INFO, "sensor%d: Temperature change %.1fC --> %.1fC",
						i+1,
						state->temp_prev[i],
						state->temp[i]);
					state->temp_prev[i] = state->temp[i];
				}
			}

			log_msg(LOG_DEBUG, "Update virtual sensors");
			for (int i = 0; i < VSENSOR_COUNT; i++) {
				state->vtemp[i] = get_vsensor(i, config, state);
				if (check_for_change(state->vtemp_prev[i], state->vtemp[i], 0.5)) {
					log_msg(LOG_INFO, "vsensor%d: Temperature change %.1fC --> %.1fC",
						i+1,
						state->vtemp_prev[i],
						state->vtemp[i]);
					state->vtemp_prev[i] = state->vtemp[i];
				}
			}
		}

		if (time_passed(&t_set_outputs, 500)) {
			log_msg(LOG_DEBUG, "Updating output signals.");
			update_outputs(state, config);
		}

		if (time_passed(&t_config, 1000)) {
			/* Attempt to update config from core0 */
			if (mutex_enter_timeout_us(config_mutex, 100)) {
				memcpy(config, cfg, sizeof(*config));
				mutex_exit(config_mutex);
			} else {
				log_msg(LOG_INFO, "failed to get config_mutex");
			}
		}
		if (time_passed(&t_state, 500)) {
			/* Attempt to update system state on core0 */
			if (mutex_enter_timeout_us(state_mutex, 100)) {
				memcpy(&transfer_state, state, sizeof(transfer_state));
				mutex_exit(state_mutex);
			} else {
				log_msg(LOG_INFO, "failed to get state_mutex");
			}
		}

	}
}


int main()
{
	absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(t_led, 0);
	absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(t_network, 0);
	absolute_time_t t_now, t_last, t_display;
	uint8_t led_state = 0;
	int64_t max_delta = 0;
	int64_t delta;
	int c;
	char input_buf[1024 + 1];
	int i_ptr = 0;


	set_binary_info();
	clear_state(&system_state);
	clear_state(&transfer_state);

	/* Initialize MCU and other hardware... */
	if (get_debug_level() >= 2)
		print_mallinfo();
	setup();
	if (get_debug_level() >= 2)
		print_mallinfo();

	/* Start second core (core1)... */
	memcpy(&core1_config, cfg, sizeof(core1_config));
	memcpy(&core1_state, &system_state, sizeof(core1_state));
	multicore_launch_core1(core1_main);

#if WATCHDOG_ENABLED
	watchdog_enable(WATCHDOG_REBOOT_DELAY, 1);
	log_msg(LOG_NOTICE, "Watchdog enabled.");
#endif

	t_last = get_absolute_time();
	t_display = t_last;

	while (1) {
		t_now = get_absolute_time();
		delta = absolute_time_diff_us(t_last, t_now);
		t_last = t_now;

		if (delta > max_delta || delta > 1000000) {
			max_delta = delta;
			log_msg(LOG_INFO, "core0: max_loop_time=%lld", max_delta);
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

		/* Update display every 1000ms */
		if (time_passed(&t_display, 1000)) {
			log_msg(LOG_DEBUG, "Update display");
			update_system_state();
			display_status(fanpico_state, cfg);
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
			if (c == 10 || c == 13 || i_ptr >= sizeof(input_buf) - 1) {
				if (cfg->local_echo) printf("\r\n");
				input_buf[i_ptr] = 0;
				if (i_ptr > 0) {
					update_system_state();
					process_command(fanpico_state, (struct fanpico_config *)cfg, input_buf);
					i_ptr = 0;
				}
				continue;
			}
			input_buf[i_ptr++] = c;
			if (cfg->local_echo) printf("%c", c);
		}
#if WATCHDOG_ENABLED
		watchdog_update();
#endif
	}
}


/* eof :-) */
