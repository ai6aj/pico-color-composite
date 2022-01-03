cmake_minimum_required(VERSION 3.12)
include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)
pico_sdk_init()

project($ENV{PICO_PROJECT_NAME})
add_subdirectory(./src)
