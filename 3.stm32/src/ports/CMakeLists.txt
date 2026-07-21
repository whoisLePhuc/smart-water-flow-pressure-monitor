add_library(fw_ports INTERFACE)
target_include_directories(fw_ports
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(fw_ports
    INTERFACE
        project_compiler_options
)
