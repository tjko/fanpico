#!/bin/sh
#
# Flash Fanpico firmware using picoprobe
#

openocd -f interface/cmsis-dap.cfg \
        -c "adapter speed 5000" \
	-f target/rp2040.cfg \
	-c "program build/fanpico.elf verify reset exit"


# eof :-)
