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
#include "pico/binary_info.h"
#include "pico/unique_id.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"

#include "cJSON.h"
#include "fanpico.h"



struct fanpico_state system_state;


void print_mallinfo()
{
	printf("mallinfo:\n");
	printf("  arena: %u\n", mallinfo().arena);
	printf(" ordblks: %u\n", mallinfo().ordblks);
	printf("uordblks: %u\n", mallinfo().uordblks);
	printf("fordblks: %u\n", mallinfo().fordblks);
}


float get_pico_temp()
{
	uint16_t raw;
	float temp, volt;
	float cal_value = 0.0;

	adc_select_input(4);
	raw = adc_read();
	volt = raw * 3.25 / (1 << 12);
	temp = 27 - ((volt - 0.706) / 0.001721) + cal_value;

	//printf("get_pico_temp(): raw=%u, volt=%f, temp=%f\n", raw, volt, temp);
	return roundf(temp);
}

void set_binary_info()
{
	bi_decl(bi_program_description("FanPico - Smart PWM Fan Controller"));
	bi_decl(bi_program_version_string(FANPICO_VERSION " ("__DATE__")"));
	bi_decl(bi_program_url("https://github.com/tjko/fanpico/"));

	bi_decl(bi_1pin_with_name(LED_PIN, "On-board LED"));

	bi_decl(bi_1pin_with_name(FAN1_TACHO_READ_PIN, "Fan1 tacho signal (input)"));
	bi_decl(bi_1pin_with_name(FAN2_TACHO_READ_PIN, "Fan2 tacho signal (input)"));
	bi_decl(bi_1pin_with_name(FAN3_TACHO_READ_PIN, "Fan3 tacho signal (input)"));
	bi_decl(bi_1pin_with_name(FAN4_TACHO_READ_PIN, "Fan4 tacho signal (input)"));
	bi_decl(bi_1pin_with_name(FAN5_TACHO_READ_PIN, "Fan5 tacho signal (input)"));
	bi_decl(bi_1pin_with_name(FAN6_TACHO_READ_PIN, "Fan6 tacho signal (input)"));
	bi_decl(bi_1pin_with_name(FAN7_TACHO_READ_PIN, "Fan7 tacho signal (input)"));
	bi_decl(bi_1pin_with_name(FAN8_TACHO_READ_PIN, "Fan8 tacho signal (input)"));

	bi_decl(bi_1pin_with_name(FAN1_PWM_GEN_PIN, "Fan1 PWM signal (output)"));
	bi_decl(bi_1pin_with_name(FAN2_PWM_GEN_PIN, "Fan2 PWM signal (output)"));
	bi_decl(bi_1pin_with_name(FAN3_PWM_GEN_PIN, "Fan3 PWM signal (output)"));
	bi_decl(bi_1pin_with_name(FAN4_PWM_GEN_PIN, "Fan4 PWM signal (output)"));
	bi_decl(bi_1pin_with_name(FAN5_PWM_GEN_PIN, "Fan5 PWM signal (output)"));
	bi_decl(bi_1pin_with_name(FAN6_PWM_GEN_PIN, "Fan6 PWM signal (output)"));
	bi_decl(bi_1pin_with_name(FAN7_PWM_GEN_PIN, "Fan7 PWM signal (output)"));
	bi_decl(bi_1pin_with_name(FAN8_PWM_GEN_PIN, "Fan8 PWM signal (output)"));

	bi_decl(bi_1pin_with_name(MBFAN1_TACHO_GEN_PIN, "MB Fan1 tacho signal (output)"));
	bi_decl(bi_1pin_with_name(MBFAN2_TACHO_GEN_PIN, "MB Fan2 tacho signal (output)"));
	bi_decl(bi_1pin_with_name(MBFAN3_TACHO_GEN_PIN, "MB Fan3 tacho signal (output)"));
	bi_decl(bi_1pin_with_name(MBFAN4_TACHO_GEN_PIN, "MB Fan4 tacho signal (output)"));

	bi_decl(bi_1pin_with_name(MBFAN1_PWM_READ_PIN, "MB Fan1 PWM signal (input)"));
	bi_decl(bi_1pin_with_name(MBFAN2_PWM_READ_PIN, "MB Fan2 PWM signal (input)"));
	bi_decl(bi_1pin_with_name(MBFAN3_PWM_READ_PIN, "MB Fan3 PWM signal (input)"));
	bi_decl(bi_1pin_with_name(MBFAN4_PWM_READ_PIN, "MB Fan4 PWM signal (input)"));
}


void setup()
{
	pico_unique_board_id_t board_id;
	int i;

	set_binary_info();
	stdio_init_all();

	sleep_ms(500);
	printf("\n\n\n");
	printf("FanPico v%s ", FANPICO_VERSION);
	printf("(Build date: %s)\n", __DATE__);
	printf("Copyright (C) 2021-2022 Timo Kokkonen <tjko@iki.fi>\n");
	printf("License GPLv3+: GNU GPL version 3 or later "
		"<https://gnu.org/licenses/gpl.html>\n"
		"This is free software: you are free to change and "
                "redistribute it.\n"
		"There is NO WARRANTY, to the extent permitted by law.\n\n");

	printf("Board: " PICO_BOARD "\n");
	printf("Serial Number: ");
	pico_get_unique_board_id(&board_id);
	for (i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++)
		printf("%02x", board_id.id[i]);
	printf("\n");
	printf("RP2040 Chip Version: %d\n", rp2040_chip_version());
	printf("RP2040 ROM Version: %d\n", rp2040_rom_version());

	read_config();

	/* Enable ADC */
	printf("Initialize ADC...\n");
	adc_init();
	adc_set_temp_sensor_enabled(true);
	adc_select_input(4);

	/* Setup GPIO pins... */
	printf("Initialize GPIO...\n");

	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	gpio_put(LED_PIN, 1);

	/* Configure PWM pins... */
	setup_pwm_outputs();
	setup_pwm_inputs();

	for (i = 0; i < FAN_MAX_COUNT; i++) {
		//set_pwm_duty_cycle(i, 10 + i * 10);
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


int check_for_change(double oldval, double newval, double treshold)
{
	double delta = fabs(oldval - newval);

	if (delta >= treshold || newval < oldval)
		return 1;

	return 0;
}


double sensor_get_temp(struct sensor_input *sensor, double temp)
{
	double newval;

	newval = (temp * sensor->temp_coefficient) + sensor->temp_offset;

	return newval;
}


double sensor_get_duty(struct sensor_input *sensor, double temp)
{
	int i;
	double a, t;
	struct temp_map *map;

	t = sensor_get_temp(sensor, temp);
	map = &sensor->map;

	if (t <= map->temp[0][0])
		return map->temp[0][1];

	i = 1;
	while (i < map->points && map->temp[i][0] < t)
		i++;

	if (t >= map->temp[i][0])
		return map->temp[i][1];

	a = (double)(map->temp[i][1] - map->temp[i-1][1]) / (double)(map->temp[i][0] - map->temp[i-1][0]);
	return map->temp[i-1][1] + a * (t - map->temp[i-1][0]);
}


double pwm_map(struct pwm_map *map, double val)
{
	int i;
	double newval;

	// value is equal or smaller than first map point
	if (val <= map->pwm[0][0])
		return map->pwm[0][1];

	// find the map points that the value falls in between of...
	i = 1;
	while(i < map->points && map->pwm[i][0] < val)
		i++;

	// value is larger or equal than last map point
	if (val >= map->pwm[i][0])
		return map->pwm[i][1];

	// calculate mapping using the map points left (i-i) and right (i) of the value...
	double a = (double)(map->pwm[i][1] - map->pwm[i-1][1]) / (double)(map->pwm[i][0] - map->pwm[i-1][0]);
	newval = map->pwm[i-1][1] + a * (val - map->pwm[i-1][0]);

	return newval;
}


double calculate_pwm_duty(struct fanpico_state *state, struct fanpico_config *config, int i)
{
	struct fan_output *fan;
	double val = 0;

	fan = &config->fans[i];

	/* get source value  */
	switch (fan->s_type) {
	case PWM_FIXED:
		val = fan->s_id;
		break;
	case PWM_MB:
		val = state->mbfan_duty[fan->s_id];
		break;
	case PWM_SENSOR:
		val = sensor_get_duty(&config->sensors[fan->s_id], state->temp[fan->s_id]);
		//printf("temp duty: %fC --> %lf\n", state->temp[fan->s_id], val);
		break;
	case PWM_FAN:
		val = state->fan_duty[fan->s_id];
		break;
	}

	/* apply mapping */
	val = pwm_map(&fan->map, val);

	/* apply coefficient */
	val *= fan->pwm_coefficient;

	/* final step to enforce min/max limits for output */
	if (val < fan->min_pwm) val = fan->min_pwm;
	if (val > fan->max_pwm) val = fan->max_pwm;

	return val;
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
		val = state->fan_freq[mbfan->s_id];
		break;
	}

	return val;
}


void update_outputs(struct fanpico_state *state, struct fanpico_config *config)
{
	int i;
	double new;

	/* update fan PWM signals */
	for (i = 0; i < FAN_MAX_COUNT; i++) {
		new = calculate_pwm_duty(state, config, i);
		if (check_for_change(state->fan_duty[i], new, 0.1)) {
			printf("Set output PWM for fan%d: %.1f --> %.1f\n",
				i+1,
				state->fan_duty[i],
				new);
			state->fan_duty[i] = new;
			set_pwm_duty_cycle(i, new);
		}
	}

	/* update mb tacho signals */
	for (i = 0; i < MBFAN_MAX_COUNT; i++) {
		new = calculate_tacho_freq(state, config, i);
		if (check_for_change(state->mbfan_freq[i], new, 0.1)) {
			printf("Set output tacho for mbfan%d: %.1f --> %.1f\n",
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
	uint8_t led_state = 0;
	int64_t delta;
	int64_t max_delta = 0;
	int c;
	char input_buf[1024];
	int i_ptr = 0;


	clear_state(st);

	/* Initialize MCU and other hardware... */
	print_mallinfo();
	setup();
	print_mallinfo();

//	print_config();
//	save_config();
//	delete_config();
	print_mallinfo();



	while (1) {
		t_now = get_absolute_time();
		delta = absolute_time_diff_us(t_last, t_now);
		t_last = t_now;

		if (delta > max_delta) {
			max_delta = delta;
			printf("%llu: max_loop_time=%lld\n", t_now, max_delta);
		}

		/* Toggle LED every 1000ms */
		if (time_passed(&t_led, 1000)) {
			led_state = (led_state > 0 ? 0 : 1);
			gpio_put(LED_PIN, led_state);
		}


		if (time_passed(&t_poll_pwm, 1500)) {
			int i;
			float new_duty;

			get_pwm_duty_cycles();
			for (i = 0; i < MBFAN_MAX_COUNT; i++) {
				new_duty = roundf(mbfan_pwm_duty[i]);
				if (check_for_change(st->mbfan_duty[i], new_duty, 0.5)) {
					printf("mbfan%d: duty cycle change %.1f --> %.1f\n",
						i+1,
						st->mbfan_duty[i],
						new_duty);
					st->mbfan_duty[i] = new_duty;
					t_set_outputs = 0;
				}
			}
		}

		if (time_passed(&t_temp, 10000)) {
			float temp = get_pico_temp();

			if (check_for_change(st->temp[0], temp, 0.5)) {
				printf("temperature change %.1fC --> %.1fC\n",
					st->temp[0],
					temp);
				st->temp[0] = temp;
			}
		}

		if (time_passed(&t_poll_tacho, 2000)) {
			int i;
			float new_freq;

			update_tacho_input_freq();
			for (i = 0; i < FAN_MAX_COUNT; i++) {
				new_freq = roundf(fan_tacho_freq[i]*10)/10.0;
				if (check_for_change(st->fan_freq[i], new_freq, 1.0)) {
					printf("fan%d: tacho frequency change %.1f --> %.1f (%f)\n",
						i+1,
						st->fan_freq[i],
						new_freq,
						fan_tacho_freq[i]);
					st->fan_freq[i] = new_freq;
				}
			}
		}

		if (time_passed(&t_set_outputs, 5000)) {
			update_outputs(st, cfg);
		}

		while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
			// printf("input: '%c' [0x%02x]\n", c, c);
			if (c == 0xff)
				continue;
			if (c == 0x7f || c == 0x08) {
				if (i_ptr > 0) i_ptr--;
				continue;
			}
			if (c == 10 || c == 13 || i_ptr >= sizeof(input_buf)) {
				input_buf[i_ptr] = 0;
				if (i_ptr > 0) {
					process_command(st, cfg, input_buf);
					i_ptr = 0;
				}
				continue;
			}
			input_buf[i_ptr++] = c;
		}

		//sleep_ms(1);
	}
}


/* eof :-) */
