# lcd-background.s
# Copyright (C) 2021-2023 Timo Kokkonen <tjko@iki.fi>
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This file is part of FanPico.
#

#
# Stub for embedding LCD background images in the executable.
#

	.global fanpico_theme_default_320x240_bmp
	.global fanpico_theme_default_480x320_bmp

	.section .rodata

	.p2align 4
fanpico_theme_default_320x240_bmp:
	.incbin	"default-320x240.bmp"

	.p2align 4
fanpico_theme_default_480x320_bmp:
	.incbin	"default-480x320.bmp"


# eof
