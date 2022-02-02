/* fanpico.h
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

#ifndef FANPICO_H
#define FANPICO_H 1


#define FAN_MAX_COUNT 8
#define MBFAN_MAX_COUNT 4

#define LED_PIN 25

/* Pins for Fan Signals */

#define FAN1_TACHO_READ_PIN 2
#define FAN2_TACHO_READ_PIN 3
#define FAN3_TACHO_READ_PIN 20
#define FAN4_TACHO_READ_PIN 21
#define FAN5_TACHO_READ_PIN 22
#define FAN6_TACHO_READ_PIN 26
#define FAN7_TACHO_READ_PIN 27
#define FAN8_TACHO_READ_PIN 28

#define FAN1_PWM_GEN_PIN 4  /* PWM2A */
#define FAN2_PWM_GEN_PIN 5  /* PWM2B */
#define FAN3_PWM_GEN_PIN 6  /* PWM3A */
#define FAN4_PWM_GEN_PIN 7  /* PWM3B */
#define FAN5_PWM_GEN_PIN 8  /* PWM4A */
#define FAN6_PWM_GEN_PIN 9  /* PWM4B */
#define FAN7_PWM_GEN_PIN 10 /* PWM5A */
#define FAN8_PWM_GEN_PIN 11 /* PWM5B */


/* Pins for Motherboar Fan Connector Signals */

#define MBFAN1_TACHO_GEN_PIN 12
#define MBFAN2_TACHO_GEN_PIN 14
#define MBFAN3_TACHO_GEN_PIN 16
#define MBFAN4_TACHO_GEN_PIN 18

#define MBFAN1_PWM_READ_PIN 13 /* PWM6B */
#define MBFAN2_PWM_READ_PIN 15 /* PWM7B */
#define MBFAN3_PWM_READ_PIN 17 /* PWM0B */
#define MBFAN4_PWM_READ_PIN 19 /* PWM1B */




/* config.c */
void read_config();


/* pwm.c */
void set_pwm_duty_cycle(uint pin, float duty);
float get_pwm_duty_cycle(uint pin);
void get_pwm_duty_cycles(uint *pins, uint count, float *duty);
void setup_pwm_outputs();
void setup_pwm_inputs();


/* tacho.c */
extern float fan_tacho_freq[FAN_MAX_COUNT];
void setup_tacho_inputs();
void setup_tacho_outputs();
void update_tacho_freq();


/* crc32.c */
unsigned int xcrc32 (const unsigned char *buf, int len, unsigned int init);


#endif /* FANPICO_H */
