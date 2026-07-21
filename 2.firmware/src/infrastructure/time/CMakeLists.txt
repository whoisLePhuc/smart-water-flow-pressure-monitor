add_library(fw_infra_time STATIC monotonic_scheduler.c)
target_include_directories(fw_infra_time PUBLIC
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fw_infra_time PUBLIC core project_compiler_options)
target_add_sanitizers(fw_infra_time)
