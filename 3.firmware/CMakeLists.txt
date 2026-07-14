cmake_minimum_required(VERSION 3.20)
project(swfpm-firmware
    VERSION 0.1.0
    DESCRIPTION "Smart Water Flow and Pressure Monitor - Firmware Core"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(cmake/warnings.cmake)

enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/platform/linux)
add_subdirectory(apps/linux_sim)
add_subdirectory(tests)
