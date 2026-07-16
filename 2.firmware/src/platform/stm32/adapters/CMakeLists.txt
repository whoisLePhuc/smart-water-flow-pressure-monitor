add_library(fw_adapter_stm32 STATIC
    adc_port_stm32.c
    i2c_port_stm32.c
    spi_port_stm32.c
)
target_include_directories(fw_adapter_stm32 PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fw_adapter_stm32 PUBLIC fw_ports project_compiler_options)
target_add_sanitizers(fw_adapter_stm32)
