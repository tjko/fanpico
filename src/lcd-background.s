# lcd-background.s
# Copyright (C) 2021-2022 Timo Kokkonen <tjko@iki.fi>
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This file is part of FanPico.
#

#
# Stub for embedding LCD background images in the executable.
#

	.global fanpico_lcd_bg_1_bmp
	.global fanpico_lcd_bg_1_bmp_end

	.section .rodata

	.p2align 4
fanpico_lcd_bg_1_bmp:	
	.incbin	"lcd-background-1.bmp"
fanpico_lcd_bg_1_bmp_end:

# eof
