add_library(fw_domain_common INTERFACE)
target_include_directories(fw_domain_common
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(fw_domain_common
    INTERFACE
        project_compiler_options
)
