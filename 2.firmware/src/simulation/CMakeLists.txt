add_library(simulation STATIC
    sim_harness.c
    scenario_manifest.c
    scenario_runner.c
    normalized_trace.c
)

target_include_directories(simulation
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(simulation
    PUBLIC
        platform_linux
        core
        infrastructure
        drivers
        services
        project_compiler_options
)

target_add_sanitizers(simulation)
