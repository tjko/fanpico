#!/bin/sh
#
# Launch openocd for debugging.
#

openocd -f interface/picoprobe.cfg -f target/rp2040.cfg -c "adapter speed 5000"


# eof :-)
