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



extern const uint FAN1_TACHO_READ_PIN;
extern const uint FAN2_TACHO_READ_PIN;
extern const uint FAN3_TACHO_READ_PIN;
extern const uint FAN4_TACHO_READ_PIN;
extern const uint FAN5_TACHO_READ_PIN;
extern const uint FAN6_TACHO_READ_PIN;
extern const uint FAN7_TACHO_READ_PIN;
extern const uint FAN8_TACHO_READ_PIN;

extern const uint FAN1_PWM_GEN_PIN;  /* PWM2A */
extern const uint FAN2_PWM_GEN_PIN;  /* PWM2B */
extern const uint FAN3_PWM_GEN_PIN;  /* PWM3A */
extern const uint FAN4_PWM_GEN_PIN;  /* PWM3B */
extern const uint FAN5_PWM_GEN_PIN;  /* PWM4A */
extern const uint FAN6_PWM_GEN_PIN;  /* PWM4B */
extern const uint FAN7_PWM_GEN_PIN; /* PWM5A */
extern const uint FAN8_PWM_GEN_PIN; /* PWM5B */


/* Pins for Motherboar Fan Connector Signals */

extern const uint MBFAN1_TACHO_GEN_PIN;
extern const uint MBFAN2_TACHO_GEN_PIN;
extern const uint MBFAN3_TACHO_GEN_PIN;
extern const uint MBFAN4_TACHO_GEN_PIN;

extern const uint MBFAN1_PWM_READ_PIN; /* PWM6B */
extern const uint MBFAN2_PWM_READ_PIN; /* PWM7B */
extern const uint MBFAN3_PWM_READ_PIN; /* PWM0B */
extern const uint MBFAN4_PWM_READ_PIN; /* PWM1B */




/* pwm.c */
void set_pwm_duty_cycle(uint pin, float duty);
float get_pwm_duty_cycle(uint pin);
void get_pwm_duty_cycles(uint *pins, uint count, float *duty);
void setup_pwm_outputs();
void setup_pwm_inputs();


/* crc32.c */
unsigned int xcrc32 (const unsigned char *buf, int len, unsigned int init);


#endif /* FANPICO_H */
