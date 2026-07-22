cmake_minimum_required(VERSION 3.22)

project(swfpm_stm32_portable_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

get_filename_component(SWFPM_STM32_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_executable(test_app_composition_boot
    test_app_composition_boot.c
    ${SWFPM_STM32_ROOT}/src/app/app_composition.c
    ${SWFPM_STM32_ROOT}/src/infrastructure/bus/i2c_bus_manager.c
    ${SWFPM_STM32_ROOT}/src/drivers/storage/fram_driver.c
    ${SWFPM_STM32_ROOT}/src/protocols/storage/storage_record.c
    ${SWFPM_STM32_ROOT}/src/protocols/storage/ab_slot.c
    ${SWFPM_STM32_ROOT}/src/services/storage/storage_service.c
)

target_include_directories(test_app_composition_boot PRIVATE
    ${SWFPM_STM32_ROOT}/src
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_app_composition_boot PRIVATE
        -Wall -Wextra -Werror -pedantic
    )
endif()

enable_testing()
add_test(NAME test_app_composition_boot COMMAND test_app_composition_boot)
