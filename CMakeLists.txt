# CMakeLists.txt for fanpico
#

cmake_minimum_required(VERSION 3.13)

#include(pico_sdk_import.cmake)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)


project(fanpico_project C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)


#set(CMAKE_BUILD_TYPE Debug)
message("Build type: ${CMAKE_BUILD_TYPE}")

pico_sdk_init()


add_subdirectory(../pico-littlefs/lfs lfs)
#add_library(lfs STATIC ../pico-littlefs/lfs/*.h ../pico-littlefs/lfs/*.c)

#add_subdirectory(../cJSON/ cJSON)
#add_library(cJSON STATIC ../cJSON/cJSON.h ../cJSON/cJSON.c)
add_library(cJSON INTERFACE)
target_include_directories(cJSON INTERFACE ../cJSON)
target_sources(cJSON INTERFACE ../cJSON/cJSON.h ../cJSON/cJSON.c)



set_property(SOURCE default_config.s APPEND PROPERTY COMPILE_OPTIONS -I${CMAKE_CURRENT_LIST_DIR})

add_executable(fanpico
  fanpico.c
  command.c
  config.c
  pwm.c
  tacho.c
  square_wave_gen.c
  crc32.c
  default_config.s
  )


pico_generate_pio_header(fanpico ${CMAKE_CURRENT_LIST_DIR}/square_wave_gen.pio)




pico_enable_stdio_usb(fanpico 1)
pico_enable_stdio_uart(fanpico 1)
pico_add_extra_outputs(fanpico)
target_link_libraries(fanpico
  pico_stdlib
  hardware_pwm
  hardware_pio
  hardware_adc
  lfs
  cJSON
  )

target_compile_options(fanpico PRIVATE -Wall)
target_compile_definitions(fanpico PRIVATE PARAM_ASSERTIONS_ENABLE_ALL=1) 
target_compile_definitions(fanpico PRIVATE PICO_MALLOC_PANIC=0)
