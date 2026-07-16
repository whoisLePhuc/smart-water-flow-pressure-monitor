cmake_minimum_required(VERSION 3.20)
project(swfpm-firmware
    VERSION 0.1.0
    DESCRIPTION "Smart Water Flow and Pressure Monitor - Firmware Core"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/warnings.cmake)
enable_testing()

# Common include paths for all targets (must be before any add_subdirectory)
set(FIRMWARE_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/infrastructure
    ${CMAKE_SOURCE_DIR}/src/infrastructure/event
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_SOURCE_DIR}/src/drivers
    ${CMAKE_SOURCE_DIR}/src/services
    ${CMAKE_SOURCE_DIR}/src/platform/include
    ${CMAKE_SOURCE_DIR}/src/platform/linux
    ${CMAKE_SOURCE_DIR}/src/platform/linux/providers
    ${CMAKE_SOURCE_DIR}/src/platform/linux/peers
    ${CMAKE_SOURCE_DIR}/src/simulation
)
add_library(firmware_includes INTERFACE)
target_include_directories(firmware_includes INTERFACE ${FIRMWARE_INCLUDE_DIRS})

add_subdirectory(src/domain)
add_subdirectory(src/core)
add_subdirectory(src/infrastructure)
add_subdirectory(src/protocols)
add_subdirectory(src/app)
add_subdirectory(src/drivers)
add_subdirectory(src/services)
add_subdirectory(src/simulation)
add_subdirectory(src/platform/linux)
add_subdirectory(apps/linux_sim)
add_subdirectory(tests)
