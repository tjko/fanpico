#!/bin/sh

openocd -f interface/picoprobe.cfg -f target/rp2040.cfg -c "program build/src/fanpico.elf verify reset exit"



