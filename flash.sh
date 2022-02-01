#!/bin/sh

openocd -f interface/picoprobe.cfg -f target/rp2040.cfg -c "program build/fanpico.elf verify reset exit"



