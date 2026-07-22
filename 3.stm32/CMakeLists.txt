cmake_minimum_required(VERSION 3.22)

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_PROJECT_NAME swfpm_app)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

enable_language(C ASM)
add_executable(${CMAKE_PROJECT_NAME})

# STM32CubeMX-generated HAL, startup and Core sources.
add_subdirectory(cmake/stm32cubemx)

# Temporary targets for dependencies that have not been ported yet.
add_library(project_compiler_options INTERFACE)
add_library(core INTERFACE)
add_library(fw_infra_event INTERFACE)
add_library(fw_service_volume INTERFACE)
add_library(fw_infra_repository INTERFACE)
add_library(fw_infra_time INTERFACE)
add_library(fw_infra_runtime INTERFACE)
add_library(fw_service_measurement INTERFACE)
add_library(fw_facade_measurement INTERFACE)
add_library(fw_facade_storage INTERFACE)
add_library(fw_facade_connectivity INTERFACE)
add_library(fw_facade_power INTERFACE)

# Portable modules. App is added last because it composes the other targets.
add_subdirectory(src/ports)
add_subdirectory(src/drivers)
add_subdirectory(src/infrastructure)
add_subdirectory(src/protocols)
add_subdirectory(src/services)
add_subdirectory(src/app)
add_subdirectory(tests)

target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user-defined library search paths here.
)

# STM32-specific implementations of portable ports.
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    src/platform/stm32/stm32_i2c1_hal.c
    src/platform/stm32/adapters/i2c_port_stm32.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user-defined symbols here.
)

list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    app
    fw_infra_bus
    fw_driver_storage
    fw_service_storage
    fw_protocol_storage
)
