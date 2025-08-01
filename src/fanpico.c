/* fanpico.c
   Copyright (C) 2021-2025 Timo Kokkonen <tjko@iki.fi>

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
#include "pico/aon_timer.h"
#ifdef LIB_PICO_CYW43_ARCH
#include "pico/cyw43_arch.h"
#endif
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "hardware/vreg.h"

#include "fanpico.h"
#include "command_util.h"
#include "psram.h"


#ifdef FANPICO_PSRAM_PIN
 #if TX_PIN == 0
  /* Check for non-standard boards that may have PSRAM on pin 0 (Serial TX) */
  #if FANPICO_PSRAM_PIN == TX_PIN
   #undef TTL_SERIAL
  #endif
 #endif
#endif

static struct fanpico_state core1_state;
static struct fanpico_config core1_config;
static struct fanpico_state transfer_state;
static struct fanpico_state system_state;
const struct fanpico_state *fanpico_state = &system_state;
static struct fanpico_fw_settings system_settings;
const struct fanpico_fw_settings *firmware_settings = &system_settings;

#ifdef WIFI_SUPPORT
static struct fanpico_network_state network_state;
struct fanpico_network_state *net_state = &network_state;
#endif

struct persistent_memory_block __uninitialized_ram(persistent_memory);
struct persistent_memory_block *persistent_mem = &persistent_memory;

#define PERSISTENT_MEMORY_ID 0x42c0ffee
#define PERSISTENT_MEMORY_CRC_LEN offsetof(struct persistent_memory_block, crc32)

auto_init_mutex(pmem_mutex_inst);
mutex_t *pmem_mutex = &pmem_mutex_inst;
auto_init_mutex(state_mutex_inst);
mutex_t *state_mutex = &state_mutex_inst;
bool rebooted_by_watchdog = false;

static char input_buf[1024];


static void update_persistent_memory_crc()
{
	struct persistent_memory_block *m = persistent_mem;

	m->crc32 = xcrc32((unsigned char*)m, PERSISTENT_MEMORY_CRC_LEN, 0);
}

static void init_persistent_memory()
{
	struct persistent_memory_block *m = persistent_mem;
	uint32_t crc;

	if (m->id == PERSISTENT_MEMORY_ID && m->len == sizeof(persistent_memory)) {
		crc = xcrc32((unsigned char*)m, PERSISTENT_MEMORY_CRC_LEN, 0);
		if (crc == m->crc32) {
			printf("Found persistent memory block\n");
			if (strlen(m->timezone) > 0) {
				/* Restore timezone */
				setenv("TZ", m->timezone, 1);
				tzset();
			}
			if (m->saved_time.tv_sec > 0)
				aon_timer_start(&m->saved_time);
			if (m->uptime) {
				m->prev_uptime = m->uptime;
				m->total_uptime += m->uptime;
			}
			m->warmstart++;
			update_persistent_memory_crc();
			return;
		}
		printf("Found corrupt persistent memory block"
			" (CRC-32 mismatch  %08lx != %08lx)\n", crc, m->crc32);
	}

	printf("Initializing persistent memory block...\n");
	memset(m, 0, sizeof(*m));
	m->id = PERSISTENT_MEMORY_ID;
	m->len = sizeof(persistent_memory);
	update_persistent_memory_crc();
}

void update_persistent_memory()
{
	struct persistent_memory_block *m = persistent_mem;
	struct timespec t;

	if (mutex_enter_timeout_us(pmem_mutex, 100)) {
		if (aon_timer_is_running()) {
			aon_timer_get_time(&t);
			m->saved_time = t;
		}
		m->uptime = to_us_since_boot(get_absolute_time());
		update_persistent_memory_crc();
		mutex_exit(pmem_mutex);
	} else {
		log_msg(LOG_DEBUG, "update_persistent_memory(): Failed to get pmem_mutex");
	}
}

void update_persistent_memory_tz(const char *tz)
{
	struct persistent_memory_block *m = persistent_mem;

	if (mutex_enter_timeout_us(pmem_mutex, 100)) {
		strncopy(m->timezone, tz, sizeof(m->timezone));
		update_persistent_memory_crc();
		mutex_exit(pmem_mutex);
	} else {
		log_msg(LOG_DEBUG, "update_persistent_memory_tz(): Failed to get pmem_mutex");
	}
}


void boot_reason()
{
#if PICO_RP2040
	printf("     CHIP_RESET: %08lx\n", vreg_and_chip_reset_hw->chip_reset);
#endif
	printf("WATCHDOG_REASON: %08lx\n", watchdog_hw->reason);
}


static void setup()
{
	struct persistent_memory_block *m = persistent_mem;
	char buf[32];
	int i = 0;

	if (firmware_settings->sysclock > 0) {
		set_sys_clock_khz(firmware_settings->sysclock * 1000, true);
		sleep_ms(100);
	}

	stdio_usb_init();
	/* Wait a while for USB Serial to connect... */
	while (i++ < 40) {
		if (stdio_usb_connected())
			break;
		sleep_ms(50);
	}

	if (firmware_settings->bootdelay > 0)
		sleep_ms(firmware_settings->bootdelay * 1000);

	lfs_setup(false);
	read_config(firmware_settings->safemode);


#if TTL_SERIAL
	/* Initialize serial console if configured... */
	if(cfg->serial_active && (!cfg->spi_active || !SPI_SHARED)) {
		stdio_uart_init_full(TTL_SERIAL_UART,
				TTL_SERIAL_SPEED, TX_PIN, RX_PIN);
	}
#endif
	printf("\n\n");
#ifndef NDEBUG
	boot_reason();
#endif
	if (watchdog_enable_caused_reboot()) {
		printf("\n[Rebooted by watchdog]\n");
		rebooted_by_watchdog = true;
	}
	printf("\n");
	psram_setup();

	/* Run "SYStem:VERsion?" command... */
	cmd_version(NULL, NULL, 0, NULL);
	/* Run "SYStem:BOARD?" command... */
	cmd_board(NULL, NULL, 0, NULL);
	printf("\n");

	init_persistent_memory();
	printf("\n");
	if (firmware_settings->safemode)
		printf("*** Booting into Safe Mode ***\n\n");

	log_msg(LOG_NOTICE, "System starting...");
	if (m->prev_uptime) {
		uptime_to_str(buf, sizeof(buf), m->prev_uptime, true);
		log_msg(LOG_NOTICE, "Uptime before last soft reset: %s", buf);
		uptime_to_str(buf, sizeof(buf), m->total_uptime, true);
		log_msg(LOG_NOTICE, "Uptime since cold boot: %s (soft reset count: %lu)",
			buf, m->warmstart);
	}

	/* Setup timezone */
	if (strlen(cfg->timezone) > 1) {
		log_msg(LOG_NOTICE, "Set timezone: %s", cfg->timezone);
		update_persistent_memory_tz(cfg->timezone);
		setenv("TZ", cfg->timezone, 1);
		tzset();
	}


	if (aon_timer_is_running()) {
		struct timespec ts;
		aon_timer_get_time(&ts);
		log_msg(LOG_NOTICE, "RTC clock time: %s",
			time_t_to_str(buf, sizeof(buf), timespec_to_time_t(&ts)));
	}

	setup_i2c_bus((struct fanpico_config *)cfg);
	display_init();
	network_init();

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
	cyw43_arch_lwip_begin();
	cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
	cyw43_arch_lwip_end();
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

	/* Configure 1-Wire pins... */
	setup_onewire_bus();

	log_msg(LOG_NOTICE, "System initialization complete.");
}


static void clear_state(struct fanpico_state *s)
{
	int i;

	memset(s, 0, sizeof(struct fanpico_state));

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
		s->vtemp_updated[i] = from_us_since_boot(0);
		s->vpressure[i] = -1.0;
		s->vhumidity[i] = -1.0;
	}

}


/* update_system_state()
 *  System state gets updated by core1 periodically (using state_mutex)
 *  to intermediate buffer transfer_state.
 *  This function updates system state from that buffer.
 */

static void update_system_state()
{
	if (mutex_enter_timeout_us(state_mutex, 1000)) {
		memcpy(&system_state, &transfer_state, sizeof(system_state));
		mutex_exit(state_mutex);
	} else {
		log_msg(LOG_INFO, "update_system_state(): failed to get system_mutex");
	}
}


static void update_outputs(struct fanpico_state *state, const struct fanpico_config *config)
{
	int i;

	/* Update fan PWM signals */
	for (i = 0; i < FAN_COUNT; i++) {
		float hyst = cfg->fans[i].pwm_hyst;
		state->fan_duty[i] = calculate_pwm_duty(state, config, i);
		if (check_for_change(state->fan_duty_prev[i], state->fan_duty[i], hyst)) {
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
			if (cfg->mbfans[i].rpm_mode == RMODE_TACHO) {
				set_tacho_output_freq(i, state->mbfan_freq[i]);
			} else {
				int rpm = state->mbfan_freq[i] * 60 / cfg->mbfans[i].rpm_factor;
				bool lra = (rpm < cfg->mbfans[i].lra_treshold ? true : false);
				set_lra_output(i, cfg->mbfans[i].lra_invert ? !lra : lra);
			}
		}
	}
}


static void core1_main()
{
	struct fanpico_config *config = &core1_config;
	struct fanpico_state *state = &core1_state;
	absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(t_temp, 0);
	absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(t_onewire_temp, 0);
	absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(t_tacho, 0);
	absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(t_poll_pwm, 0);
	absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(t_set_outputs, 0);
	absolute_time_t t_last, t_now, t_config, t_state;
	int onewire_delay = 5000;
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

		if (delta > max_delta) {
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
			//log_msg(LOG_DEBUG, "Read PWM inputs");
			for (int i = 0; i < MBFAN_COUNT; i++) {
				state->mbfan_duty[i] = roundf(mbfan_pwm_duty[i]);
				if (check_for_change(state->mbfan_duty_prev[i], state->mbfan_duty[i], 1.5)) {
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

		/* Read 1-Wire temperature sensors */
		if (config->onewire_active && onewire_delay > 0) {
			if (time_passed(&t_onewire_temp, onewire_delay)) {
				onewire_delay = onewire_read_temps(config, state);
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
				log_msg(LOG_DEBUG, "failed to get config_mutex");
			}
		}
		if (time_passed(&t_state, 500)) {
			/* Attempt to update system state on core0 */
			if (mutex_enter_timeout_us(state_mutex, 100)) {
				memcpy(&transfer_state, state, sizeof(transfer_state));
				mutex_exit(state_mutex);
			} else {
				log_msg(LOG_DEBUG, "failed to get state_mutex");
			}
		}

	}
}


int main()
{
	absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(t_led, 0);
	absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(t_network, 0);
	absolute_time_t ABSOLUTE_TIME_INITIALIZED_VAR(t_watchdog, 0);
	absolute_time_t t_now, t_last, t_display, t_ram, t_i2c_temp, t_led_start;
	uint8_t led_state = 0;
	int64_t max_delta = 0;
	int64_t led_max_delta = 0;
	int64_t delta;
	int c;
	int i_ptr = 0;
	int i2c_temp_delay =  1000;

	set_binary_info(&system_settings);
	clear_state(&system_state);
	clear_state(&transfer_state);
#ifdef WIFI_SUPPORT
	memset(&network_state, 0, sizeof(network_state));
#endif

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
	t_i2c_temp = t_ram = t_display = t_last;

	while (1) {
		t_now = get_absolute_time();
		delta = absolute_time_diff_us(t_last, t_now);
		t_last = t_now;

		if (delta > max_delta) {
			max_delta = delta;
			log_msg(LOG_INFO, "core0: max_loop_time=%lld", max_delta);
		}

		if (time_passed(&t_network, 1)) {
			network_poll();
		}
		if (time_passed(&t_ram, 1000)) {
			log_msg(LOG_DEBUG, "update persistent mem start");
			update_persistent_memory();
			//log_msg(LOG_DEBUG, "update persistent mem end");
		}

		/* Toggle LED every 1000ms */
		if (time_passed(&t_led, 1000)) {
			uint8_t old_led_state = led_state;
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
			if (led_state != old_led_state) {
				log_msg(LOG_DEBUG, "toggle LED start: %u", led_state);
				t_led_start = get_absolute_time();
#if LED_PIN > 0
				gpio_put(LED_PIN, led_state);
#endif
#ifdef LIB_PICO_CYW43_ARCH
				cyw43_arch_lwip_begin();
				cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
				cyw43_arch_lwip_end();
#endif
				t_now = get_absolute_time();
				log_msg(LOG_DEBUG, "toggle LED end");
				delta = absolute_time_diff_us(t_led_start, t_now);
				if (delta > led_max_delta) {
					led_max_delta = delta;
					log_msg(LOG_INFO, "core0: max_led_gpio_time=%lld", led_max_delta);
				}
			}
		}

		/* Update display every 1000ms */
		if (time_passed(&t_display, 1000)) {
			log_msg(LOG_DEBUG, "update display start");
			update_system_state();
			display_status(fanpico_state, cfg);
			log_msg(LOG_DEBUG, "update display end");
		}

		/* Poll I2C Temperature Sensors */
		if (i2c_temp_delay > 0 && time_passed(&t_i2c_temp, i2c_temp_delay)) {
			//log_msg(LOG_DEBUG, "I2C sensor poll start");
			i2c_temp_delay = i2c_read_temps((struct fanpico_config*)cfg);
			//log_msg(LOG_DEBUG, "I2C sensor poll end");
		}

		/* Process any (user) input */
		while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
			//log_msg(LOG_DEBUG, "character received: %02x", c);
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
					log_msg(LOG_DEBUG,"user command start");
					update_system_state();
					process_command(fanpico_state, (struct fanpico_config *)cfg, input_buf);
					log_msg(LOG_DEBUG,"user command end");
					i_ptr = 0;
				}
				continue;
			}
			input_buf[i_ptr++] = c;
			if (cfg->local_echo) printf("%c", c);
		}
		if (time_passed(&t_watchdog, 1000)) {
#if WATCHDOG_ENABLED
			log_msg(LOG_DEBUG,"watchdog update");
			watchdog_update();
#endif
		}
	}
}


/* eof :-) */
