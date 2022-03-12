#!/bin/sh
#
# Flash Fanpico firmware using picoprobe
#

openocd -f interface/picoprobe.cfg \
	-f target/rp2040.cfg \
	-c "program build/src/fanpico.elf verify reset exit"


# eof :-)
