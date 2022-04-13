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


# External libaries

add_subdirectory(../littlefs-lib littlefs-lib)

#add_subdirectory(../cJSON/ cJSON)
add_library(cJSON INTERFACE)
target_include_directories(cJSON INTERFACE ../cJSON)
target_sources(cJSON INTERFACE ../cJSON/cJSON.h ../cJSON/cJSON.c)


# Fanpico firmware sources

add_subdirectory(src firmware)

