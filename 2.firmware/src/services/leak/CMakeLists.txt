add_library(fw_service_leak STATIC leak_config.c leak_tracker.c leak_detection.c)
target_include_directories(fw_service_leak PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fw_service_leak PUBLIC
    fw_infra_event fw_infra_time fw_infra_repository project_compiler_options)
target_add_sanitizers(fw_service_leak)
