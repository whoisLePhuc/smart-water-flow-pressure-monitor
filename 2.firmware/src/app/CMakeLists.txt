add_library(app STATIC
    system_fsm.c
    mode_guard.c
)

target_include_directories(app
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/platform/include
)

target_link_libraries(app
    PUBLIC
        infrastructure
        project_compiler_options
)

target_add_sanitizers(app)
