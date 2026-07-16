add_library(fw_protocol_telemetry INTERFACE)
target_include_directories(fw_protocol_telemetry
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(fw_protocol_telemetry
    INTERFACE
        project_compiler_options
)
