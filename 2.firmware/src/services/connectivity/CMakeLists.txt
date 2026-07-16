add_library(fw_service_connectivity STATIC
    reporting_schedule.c telemetry_queue.c cellular_delivery.c)
target_include_directories(fw_service_connectivity PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fw_service_connectivity PUBLIC
    fw_protocol_telemetry fw_infra_event fw_infra_time project_compiler_options)
target_add_sanitizers(fw_service_connectivity)
