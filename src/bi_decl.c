/* bi_decl.c
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
#include "pico/binary_info.h"

#include "fanpico.h"


void set_binary_info()
{
	bi_decl(bi_program_description("FanPico-" FANPICO_MODEL " - Smart PWM Fan Controller"));
	bi_decl(bi_program_version_string(FANPICO_VERSION " ("__DATE__")"));
	bi_decl(bi_program_url("https://github.com/tjko/fanpico/"));

#if LED_PIN >= 0
	bi_decl(bi_1pin_with_name(LED_PIN, "On-board LED (output)"));
#endif

#if TX_PIN >= 0
	bi_decl(bi_1pin_with_name(TX_PIN, "TX (Serial) / MISO (SPI)"));
	bi_decl(bi_1pin_with_name(RX_PIN, "RX (Serial) / CS (SPI)"));
#endif
#if SDA_PIN >= 0
	bi_decl(bi_1pin_with_name(SDA_PIN, "SDA (I2C) / SCK (SPI)"));
	bi_decl(bi_1pin_with_name(SCL_PIN, "SCL (I2C) / MOSI (SPI)"));
#endif

#if FAN_TACHO_READ_PIN >= 0
	bi_decl(bi_1pin_with_name(FAN_TACHO_READ_PIN, "Multiplexer A [tacho signal] (input)"));
	bi_decl(bi_1pin_with_name(FAN_TACHO_READ_S0_PIN, "Multiplexer S0 (output)"));
	bi_decl(bi_1pin_with_name(FAN_TACHO_READ_S1_PIN, "Multiplexer S1 (output)"));
	bi_decl(bi_1pin_with_name(FAN_TACHO_READ_S2_PIN, "Multiplexer S2 (output)"));
#endif


#if FAN1_TACHO_READ_PIN >= 0
	bi_decl(bi_1pin_with_name(FAN1_TACHO_READ_PIN, "Fan1 tacho signal (input)"));
#endif
#if FAN2_TACHO_READ_PIN >= 0
	bi_decl(bi_1pin_with_name(FAN2_TACHO_READ_PIN, "Fan2 tacho signal (input)"));
#endif
#if FAN3_TACHO_READ_PIN >= 0
	bi_decl(bi_1pin_with_name(FAN3_TACHO_READ_PIN, "Fan3 tacho signal (input)"));
#endif
#if FAN4_TACHO_READ_PIN >= 0
	bi_decl(bi_1pin_with_name(FAN4_TACHO_READ_PIN, "Fan4 tacho signal (input)"));
#endif
#if FAN5_TACHO_READ_PIN >= 0
	bi_decl(bi_1pin_with_name(FAN5_TACHO_READ_PIN, "Fan5 tacho signal (input)"));
#endif
#if FAN6_TACHO_READ_PIN >= 0
	bi_decl(bi_1pin_with_name(FAN6_TACHO_READ_PIN, "Fan6 tacho signal (input)"));
#endif
#if FAN7_TACHO_READ_PIN >= 0
	bi_decl(bi_1pin_with_name(FAN7_TACHO_READ_PIN, "Fan7 tacho signal (input)"));
#endif
#if FAN8_TACHO_READ_PIN >= 0
	bi_decl(bi_1pin_with_name(FAN8_TACHO_READ_PIN, "Fan8 tacho signal (input)"));
#endif

	bi_decl(bi_1pin_with_name(FAN1_PWM_GEN_PIN, "Fan1 PWM signal (output)"));
	bi_decl(bi_1pin_with_name(FAN2_PWM_GEN_PIN, "Fan2 PWM signal (output)"));
	bi_decl(bi_1pin_with_name(FAN3_PWM_GEN_PIN, "Fan3 PWM signal (output)"));
	bi_decl(bi_1pin_with_name(FAN4_PWM_GEN_PIN, "Fan4 PWM signal (output)"));
#if FAN5_PWM_GEN_PIN >= 0
	bi_decl(bi_1pin_with_name(FAN5_PWM_GEN_PIN, "Fan5 PWM signal (output)"));
#endif
#if FAN6_PWM_GEN_PIN >= 0
	bi_decl(bi_1pin_with_name(FAN6_PWM_GEN_PIN, "Fan6 PWM signal (output)"));
#endif
#if FAN7_PWM_GEN_PIN >= 0
	bi_decl(bi_1pin_with_name(FAN7_PWM_GEN_PIN, "Fan7 PWM signal (output)"));
#endif
#if FAN8_PWM_GEN_PIN >= 0
	bi_decl(bi_1pin_with_name(FAN8_PWM_GEN_PIN, "Fan8 PWM signal (output)"));
#endif

	bi_decl(bi_1pin_with_name(MBFAN1_TACHO_GEN_PIN, "MB Fan1 tacho signal (output)"));
#if MBFAN2_TACHO_GEN_PIN >= 0
	bi_decl(bi_1pin_with_name(MBFAN2_TACHO_GEN_PIN, "MB Fan2 tacho signal (output)"));
#endif
#if MBFAN3_TACHO_GEN_PIN >= 0
	bi_decl(bi_1pin_with_name(MBFAN3_TACHO_GEN_PIN, "MB Fan3 tacho signal (output)"));
#endif
#if MBFAN4_TACHO_GEN_PIN >= 0
	bi_decl(bi_1pin_with_name(MBFAN4_TACHO_GEN_PIN, "MB Fan4 tacho signal (output)"));
#endif

	bi_decl(bi_1pin_with_name(MBFAN1_PWM_READ_PIN, "MB Fan1 PWM signal (input)"));
#if MBFAN2_PWM_READ_PIN >= 0
	bi_decl(bi_1pin_with_name(MBFAN2_PWM_READ_PIN, "MB Fan2 PWM signal (input)"));
#endif
#if MBFAN3_PWM_READ_PIN >= 0
	bi_decl(bi_1pin_with_name(MBFAN3_PWM_READ_PIN, "MB Fan3 PWM signal (input)"));
#endif
#if MBFAN4_PWM_READ_PIN >= 0
	bi_decl(bi_1pin_with_name(MBFAN4_PWM_READ_PIN, "MB Fan4 PWM signal (input)"));
#endif

#if SENSOR1_READ_PIN >= 0
	bi_decl(bi_1pin_with_name(SENSOR1_READ_PIN, "Temperature Sensor1 (input)"));
#endif
#if SENSOR2_READ_PIN >= 0
	bi_decl(bi_1pin_with_name(SENSOR2_READ_PIN, "Temperature Sensor2 (input)"));
#endif
}



