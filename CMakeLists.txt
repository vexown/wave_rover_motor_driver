cmake_minimum_required(VERSION 3.16.0)

# Set project version (single source of truth)
set(PROJECT_VER "1.0.4")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wave_rover_motor_driver)

# Pass version to C code as compile definition
add_compile_definitions(FIRMWARE_VERSION="${PROJECT_VER}")
