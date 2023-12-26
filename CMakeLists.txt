# CMakeLists.txt for fanpico
#

cmake_minimum_required(VERSION 3.13)

# Include Pico-SDK ($PICO_SDK_PATH must be set)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)


project(fanpico
  VERSION 1.6.0
  LANGUAGES C CXX ASM
  )
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)


set(FANPICO_BOARD 0804D CACHE STRING "Fanpico Board Model")
#set_property(CACHE FANPICO_BOARD PROPERTY STRINGS 0804 0803D)
set(FANPICO_CUSTOM_THEME 0 CACHE STRING "Fanpico LCD Custom Theme")
set(FANPICO_CUSTOM_LOGO 0 CACHE STRING "Fanpico LCD Custom Logo")

set(TLS_SUPPORT 1 CACHE STRING "TLS Support")
# Generate some "random" data for mbedtls (better than nothing...)
set(EXTRA_ENTROPY_LEN 64)
string(RANDOM LENGTH ${EXTRA_ENTROPY_LEN} EXTRA_ENTROPY)

#set(CMAKE_BUILD_TYPE Debug)

message("---------------------------------")
message("       FANPICO_BOARD: ${FANPICO_BOARD}")
message("          PICO_BOARD: ${PICO_BOARD}")
message("FANPICO_CUSTOM_THEME: ${FANPICO_CUSTOM_THEME}")
message(" FANPICO_CUSTOM_LOGO: ${FANPICO_CUSTOM_LOGO}")
message("         TLS_SUPPORT: ${TLS_SUPPORT}")
message("    CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message("---------------------------------")

pico_sdk_init()


# External libaries

# littlefs-lib
add_subdirectory(libs/littlefs-lib)
# prevent warnings from littlefs...
set_source_files_properties(
  libs/littlefs-lib/lfs.c
  PROPERTIES
  COMPILE_OPTIONS "-Wno-unused-function"
  )

# ss_oled
add_subdirectory(libs/ss_oled-lib)

# bb_spi_lcd
add_subdirectory(libs/bb_spi_lcd-lib)

# cJSON
add_library(cJSON INTERFACE)
target_include_directories(cJSON INTERFACE libs/cJSON)
target_sources(cJSON INTERFACE
  libs/cJSON/cJSON.h
  libs/cJSON/cJSON.c
  )

# libb64
add_library(libb64 INTERFACE)
target_include_directories(libb64 INTERFACE libs/libb64/include)
target_sources(libb64 INTERFACE
  libs/libb64/src/cdecode.c
  libs/libb64/src/cencode.c
  )


# fanpico firmware

add_executable(fanpico
  src/fanpico.c
  src/bi_decl.c
  src/command.c
  src/flash.c
  src/config.c
  src/display.c
  src/display_lcd.c
  src/display_oled.c
  src/network.c
  src/tls.c
  src/pwm.c
  src/tacho.c
  src/sensors.c
  src/filters.c
  src/filter_lossypeak.c
  src/filter_sma.c
  src/square_wave_gen.c
  src/pulse_len.c
  src/util.c
  src/util_rp2040.c
  src/log.c
  src/crc32.c
  src/default_config.s
  src/credits.s
  src/logos/default.s
  src/themes/lcd-background.s
  )

if (FANPICO_CUSTOM_THEME)
  message("Enable custom theme...")
  target_sources(fanpico PRIVATE src/themes/custom.s)
  set_property(SOURCE src/themes/custom.s APPEND PROPERTY COMPILE_OPTIONS -I${CMAKE_CURRENT_LIST_DIR}/src/themes)
endif()

if (FANPICO_CUSTOM_LOGO)
  message("Enable custom logo...")
  target_sources(fanpico PRIVATE src/logos/custom.s)
  set_property(SOURCE src/logos/custom.s APPEND PROPERTY COMPILE_OPTIONS -I${CMAKE_CURRENT_LIST_DIR}/src/logos)
endif()

set_property(SOURCE src/*.s APPEND PROPERTY COMPILE_OPTIONS -I${CMAKE_CURRENT_LIST_DIR}/src)
set_property(SOURCE src/themes/lcd-background.s APPEND PROPERTY COMPILE_OPTIONS -I${CMAKE_CURRENT_LIST_DIR}/src/themes)
set_property(SOURCE src/logos/default.s APPEND PROPERTY COMPILE_OPTIONS -I${CMAKE_CURRENT_LIST_DIR}/src/logos)
set_property(SOURCE src/credits.s APPEND PROPERTY COMPILE_OPTIONS -I${CMAKE_CURRENT_LIST_DIR})

pico_generate_pio_header(fanpico ${CMAKE_CURRENT_LIST_DIR}/src/square_wave_gen.pio)


pico_enable_stdio_usb(fanpico 1)
pico_enable_stdio_uart(fanpico 0)
pico_add_extra_outputs(fanpico)

target_include_directories(fanpico PRIVATE src)

configure_file(src/config.h.in config.h)
configure_file(src/fanpico-compile.h.in fanpico-compile.h)

target_link_libraries(fanpico PRIVATE
  pico_stdlib
  pico_stdio_uart
  pico_multicore
  pico_rand
  hardware_pwm
  hardware_pio
  hardware_adc
  hardware_i2c
  hardware_rtc
  littlefs-lib
  cJSON
  ss_oled-lib
  bb_spi_lcd-lib
  libb64
  )

if (PICO_CYW43_SUPPORTED) # set by PICO_BOARD=pico_w
  message("Enabling WiFi stuff...")
  target_sources(fanpico PRIVATE
    src/syslog.c
    src/httpd.c
    src/mqtt.c
    )
  target_link_libraries(fanpico PRIVATE
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_sntp
    pico_lwip_http
    pico_lwip_mqtt
    )
  if (TLS_SUPPORT)
    target_link_libraries(fanpico PRIVATE
      pico_lwip_mbedtls
      pico_mbedtls
      )
    # prevent warnings from mbedtls...
    set_source_files_properties(
      ${PICO_LWIP_PATH}/src/apps/altcp_tls/altcp_tls_mbedtls.c
      PROPERTIES
      COMPILE_OPTIONS "-Wno-unused-result"
      )
  endif()
endif()

target_compile_options(fanpico PRIVATE -Wall)
target_compile_definitions(fanpico PRIVATE USBD_MANUFACTURER="TJKO Industries")
target_compile_definitions(fanpico PRIVATE USBD_PRODUCT="FanPico-${FANPICO_BOARD} Fan Controller")
target_compile_definitions(fanpico PRIVATE USBD_DESC_STR_MAX=32)
target_compile_definitions(fanpico PRIVATE PARAM_ASSERTIONS_ENABLE_ALL=1)
target_compile_definitions(fanpico PRIVATE PICO_MALLOC_PANIC=0)
target_compile_definitions(fanpico PRIVATE PICO_DEBUG_MALLOC=0)
#target_compile_definitions(fanpico PRIVATE PICO_DEOPTIMIZED_DEBUG=1)
target_compile_definitions(fanpico PRIVATE PICO_STACK_SIZE=0x2000)
target_compile_definitions(fanpico PRIVATE PICO_CORE1_STACK_SIZE=0x1000)
target_compile_definitions(fanpico PRIVATE PICO_USE_STACK_GUARDS=1)
target_compile_definitions(fanpico PRIVATE PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1)

pico_set_linker_script(fanpico ${CMAKE_SOURCE_DIR}/src/memmap_custom.ld)

# eof :-)
