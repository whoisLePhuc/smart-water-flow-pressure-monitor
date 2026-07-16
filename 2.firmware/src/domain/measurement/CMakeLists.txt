add_library(fw_domain_measurement INTERFACE)
target_include_directories(fw_domain_measurement
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(fw_domain_measurement
    INTERFACE
        fw_domain_common
        project_compiler_options
)
