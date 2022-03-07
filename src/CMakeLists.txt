# CMakeLists.txt for fanpico
#


set_property(SOURCE default_config.s APPEND PROPERTY COMPILE_OPTIONS -I${CMAKE_CURRENT_LIST_DIR})

add_executable(fanpico
  fanpico.c
  command.c
  config.c
  pwm.c
  tacho.c
  sensors.c
  square_wave_gen.c
  util.c
  crc32.c
  default_config.s
  )


pico_generate_pio_header(fanpico ${CMAKE_CURRENT_LIST_DIR}/square_wave_gen.pio)




pico_enable_stdio_usb(fanpico 1)
pico_enable_stdio_uart(fanpico 0)
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
