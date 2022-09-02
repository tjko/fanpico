# credits.s
# Copyright (C) 2021-2022 Timo Kokkonen <tjko@iki.fi>
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This file is part of FanPico.
#

#
# Stub for embedding credits.txt in the executable.
#

	.global fanpico_credits_text
	.global fanpico_credits_text_end

	.section .rodata

	.p2align 2
fanpico_credits_text:	
	.incbin	"credits.txt"
	.byte	 0x00
fanpico_credits_text_end:

# eof
