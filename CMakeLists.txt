# CMakeLists.txt for fanpico
#

cmake_minimum_required(VERSION 3.13)

#include(pico_sdk_import.cmake)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(fanpico_project C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)


pico_sdk_init()



add_executable(fanpico
  fanpico.c
  config.c
  pwm.c
  tacho.c
  square_wave_gen.c
  crc32.c
  )

pico_generate_pio_header(fanpico ${CMAKE_CURRENT_LIST_DIR}/square_wave_gen.pio)


add_subdirectory(../pico-littlefs/lfs lfs)


pico_enable_stdio_usb(fanpico 1)
pico_enable_stdio_uart(fanpico 1)
pico_add_extra_outputs(fanpico)
target_link_libraries(fanpico
  pico_stdlib
  hardware_pwm
  hardware_pio
  hardware_adc
  lfs)

target_compile_options(fanpico PRIVATE "-Wall")

