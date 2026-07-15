add_library(infrastructure STATIC
    i2c_bus_manager.c
)

target_include_directories(infrastructure
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(infrastructure
    PUBLIC
        core
        project_compiler_options
)

target_add_sanitizers(infrastructure)
