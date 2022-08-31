# CMakeLists.txt for fanpico
#

cmake_minimum_required(VERSION 3.13)

# Include Pico-SDK ($PICO_SDK_PATH must be set)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)


project(fanpico
  VERSION 1.2.0
  LANGUAGES C CXX ASM
  )
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)


set(FANPICO_BOARD 0804 CACHE STRING "Fanpico Board Model")
#set_property(CACHE FANPICO_BOARD PROPERTY STRINGS 0804 0803D)

message("Firmware target (FANPIC_BOARD): ${FANPICO_BOARD}")
#set(CMAKE_BUILD_TYPE Debug)
message("Build type: ${CMAKE_BUILD_TYPE}")

pico_sdk_init()


# External libaries

# littlefs-lib
add_subdirectory(../littlefs-lib littlefs-lib)

# cJSON
add_library(cJSON INTERFACE)
target_include_directories(cJSON INTERFACE ../cJSON)
target_sources(cJSON INTERFACE ../cJSON/cJSON.h ../cJSON/cJSON.c)

# ss_oled
add_library(ss_oled INTERFACE)
target_include_directories(ss_oled INTERFACE ../Pi_Pico_C_Projects/ss_oled)
target_sources(ss_oled INTERFACE
  ../Pi_Pico_C_Projects/ss_oled/ss_oled.h
  ../Pi_Pico_C_Projects/ss_oled/ss_oled.c
  ../Pi_Pico_C_Projects/ss_oled/BitBang_I2C.h
  ../Pi_Pico_C_Projects/ss_oled/BitBang_I2C.c
)


# fanpico firmware

add_executable(fanpico
  src/fanpico.c
  src/command.c
  src/config.c
  src/display.c
  src/pwm.c
  src/tacho.c
  src/sensors.c
  src/square_wave_gen.c
  src/util.c
  src/crc32.c
  src/default_config.s
  )


set_property(SOURCE src/default_config.s APPEND PROPERTY COMPILE_OPTIONS -I${CMAKE_CURRENT_LIST_DIR}/src)

pico_generate_pio_header(fanpico ${CMAKE_CURRENT_LIST_DIR}/src/square_wave_gen.pio)


pico_enable_stdio_usb(fanpico 1)
pico_enable_stdio_uart(fanpico 0)
pico_add_extra_outputs(fanpico)

target_include_directories(fanpico PRIVATE src)

configure_file(src/config.h.in config.h)

target_link_libraries(fanpico PRIVATE
  pico_stdlib
  hardware_pwm
  hardware_pio
  hardware_adc
  hardware_i2c
  littlefs-lib
  cJSON
  ss_oled
  )

target_compile_options(fanpico PRIVATE -Wall)
target_compile_definitions(fanpico PRIVATE PARAM_ASSERTIONS_ENABLE_ALL=1)
target_compile_definitions(fanpico PRIVATE PICO_MALLOC_PANIC=0)
target_compile_definitions(fanpico PRIVATE PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1)


# eof :-)
