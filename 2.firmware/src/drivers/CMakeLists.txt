add_library(drivers STATIC
    max35103.c
    zssc3241.c
)

target_include_directories(drivers
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(drivers
    PUBLIC
        core
        project_compiler_options
)

target_add_sanitizers(drivers)
