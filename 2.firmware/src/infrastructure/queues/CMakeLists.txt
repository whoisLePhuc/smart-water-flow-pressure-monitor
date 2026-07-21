add_library(fw_infra_queue STATIC app_event_queue.c)
target_include_directories(fw_infra_queue PUBLIC
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fw_infra_queue PUBLIC core fw_ports project_compiler_options)
target_add_sanitizers(fw_infra_queue)
