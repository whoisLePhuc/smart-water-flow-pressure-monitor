# Core library — now empty after restructure into app/, infrastructure/, domain/
# Retained as placeholder for common utilities
add_library(core INTERFACE)

target_include_directories(core
    INTERFACE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(core
    INTERFACE
        firmware_includes
        project_compiler_options
)
