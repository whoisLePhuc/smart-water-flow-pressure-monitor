add_library(simulation STATIC sim_harness.c scenario_manifest.c scenario_runner.c normalized_trace.c)
target_include_directories(simulation PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/platform/include)
target_link_libraries(simulation PUBLIC platform_linux app infrastructure project_compiler_options PRIVATE services drivers)
target_add_sanitizers(simulation)