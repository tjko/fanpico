# default.s
# Copyright (C) 2021-2023 Timo Kokkonen <tjko@iki.fi>
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This file is part of FanPico.
#

#
# Stub for embedding lcd-logo.bmp in the executable.
#

	.global fanpico_lcd_logo_bmp
	.global fanpico_lcd_logo_bmp_end

	.section .rodata

	.p2align 4
fanpico_lcd_logo_bmp:	
	.incbin	"default.bmp"
fanpico_lcd_logo_bmp_end:

# eof
