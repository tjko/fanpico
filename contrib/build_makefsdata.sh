#!/bin/sh

INCLUDES="-I ${PICO_SDK_PATH}/lib/lwip/src/include/ -I ${PICO_SDK_PATH}/lib/lwip/contrib/ports/unix/port/include/"

#gcc ${INCLUDES} -I ../build/ -I ../src/ -o makefsdata ${PICO_SDK_PATH}/lib/lwip/src/apps/http/makefsdata/makefsdata.c
gcc ${INCLUDES} -I ${PICO_SDK_PATH}/lib/lwip/src/apps/http/makefsdata -I ../src -I ../build -o makefsdata makefsdata.c


