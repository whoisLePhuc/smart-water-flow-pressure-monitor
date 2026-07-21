add_library(fw_adapter_linux STATIC
    adc_port_linux.c
    storage_port_linux.c
)
target_include_directories(fw_adapter_linux
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src/ports
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(fw_adapter_linux
    PUBLIC
        fw_ports
        project_compiler_options
)
target_add_sanitizers(fw_adapter_linux)
