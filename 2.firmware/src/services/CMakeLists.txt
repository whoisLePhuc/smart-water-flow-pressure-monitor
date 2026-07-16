# ── Granular service sub-targets ──

add_library(fw_service_measurement STATIC
    measurement_manager.c
    processing_stubs.c
)
target_include_directories(fw_service_measurement PUBLIC ${CMAKE_SOURCE_DIR}/src PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fw_service_measurement PUBLIC fw_infra_event app project_compiler_options)
target_add_sanitizers(fw_service_measurement)

add_library(fw_service_processing STATIC
    calibration_service.c
    flow_service.c
    pressure_service.c
)
target_include_directories(fw_service_processing PUBLIC ${CMAKE_SOURCE_DIR}/src PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fw_service_processing PUBLIC fw_infra_event fw_infra_time app project_compiler_options)
target_add_sanitizers(fw_service_processing)

add_library(fw_service_leak STATIC
    leak_config.c
    leak_tracker.c
    leak_detection.c
)
target_include_directories(fw_service_leak PUBLIC ${CMAKE_SOURCE_DIR}/src PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fw_service_leak PUBLIC fw_infra_event fw_infra_time fw_infra_repository app project_compiler_options)
target_add_sanitizers(fw_service_leak)

add_library(fw_service_storage STATIC
    storage_record.c
    ab_slot.c
    storage_service.c
    fram_driver.c
    checkpoint_policy.c
)
target_include_directories(fw_service_storage PUBLIC ${CMAKE_SOURCE_DIR}/src PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fw_service_storage PUBLIC fw_infra_repository app project_compiler_options)
target_add_sanitizers(fw_service_storage)

add_library(fw_service_connectivity STATIC
    time_service.c
    reporting_schedule.c
    telemetry_builder.c
    telemetry_queue.c
    cellular_delivery.c
)
target_include_directories(fw_service_connectivity PUBLIC ${CMAKE_SOURCE_DIR}/src PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fw_service_connectivity PUBLIC fw_infra_event fw_infra_time app project_compiler_options)
target_add_sanitizers(fw_service_connectivity)

# ── Compatibility target (kept for migration, removed in Phase 9) ──
add_library(services STATIC
    volume_accumulator.c
)
target_include_directories(services PUBLIC ${CMAKE_SOURCE_DIR}/src PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(services PUBLIC
    fw_service_measurement
    fw_service_processing
    fw_service_leak
    fw_service_storage
    fw_service_connectivity
    drivers
    app
    infrastructure
    project_compiler_options
)
target_add_sanitizers(services)
