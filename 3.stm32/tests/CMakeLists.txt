option(SWFPM_BUILD_FRAM_HIL_TESTS
       "Build the standalone STM32 F-RAM HIL test firmware" ON)
option(SWFPM_ENABLE_DESTRUCTIVE_STORAGE_TESTS
       "Enable tests that overwrite calibration/volume regions" ON)
set(SWFPM_FRAM_SOAK_ITERATIONS 100 CACHE STRING
    "Number of persistent checkpoint iterations in the soak test")

if(NOT SWFPM_BUILD_FRAM_HIL_TESTS)
    return()
endif()

add_executable(test_fram_storage)

target_sources(test_fram_storage PRIVATE
    fram_hil_main.c
    fram_test_groups.c

    support/stm32_test_runner.c
    support/stm32_test_reporter_uart.c
    support/stm32_test_platform.c
    support/fram_driver_fixture.c
    support/storage_fixture.c
    support/volume_storage_fixture.c

    unit/platform/test_i2c_port_stm32.c
    contract/platform/test_i2c1_hal_contract.c
    integration/storage/test_fram_driver.c
    integration/storage/test_fram_error_recovery.c
    integration/storage/test_fram_shared_bus.c
    system/storage/test_storage_boot_restore.c
    system/storage/test_storage_interrupted_commit.c
    system/storage/test_storage_soak.c

    ${CMAKE_SOURCE_DIR}/Core/Src/stm32l4xx_it.c
    ${CMAKE_SOURCE_DIR}/Core/Src/stm32l4xx_hal_msp.c
    ${CMAKE_SOURCE_DIR}/Core/Src/sysmem.c
    ${CMAKE_SOURCE_DIR}/Core/Src/syscalls.c
    ${CMAKE_SOURCE_DIR}/startup_stm32l433xx.s
    ${CMAKE_SOURCE_DIR}/src/platform/stm32/stm32_i2c1_hal.c
    ${CMAKE_SOURCE_DIR}/src/platform/stm32/adapters/i2c_port_stm32.c
)

target_include_directories(test_fram_storage PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(test_fram_storage PRIVATE
    SWFPM_FRAM_SOAK_ITERATIONS=${SWFPM_FRAM_SOAK_ITERATIONS}
    SWFPM_ENABLE_DESTRUCTIVE_STORAGE_TESTS=$<BOOL:${SWFPM_ENABLE_DESTRUCTIVE_STORAGE_TESTS}>
)

target_link_libraries(test_fram_storage PRIVATE
    stm32cubemx
    STM32_Drivers
    app
    fw_service_storage
    fw_driver_storage
    fw_infra_bus
    fw_protocol_storage
    project_compiler_options
    ${TOOLCHAIN_LINK_LIBRARIES}
)

set_target_properties(test_fram_storage PROPERTIES
    ADDITIONAL_CLEAN_FILES "test_fram_storage.map"
)
