add_library(fw_service_measurement STATIC measurement_manager.c)
target_include_directories(fw_service_measurement PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fw_service_measurement PUBLIC
    fw_infra_event fw_infra_repository fw_infra_bus fw_ports
    fw_driver_max35103 fw_driver_zssc3241 fw_service_processing
    fw_service_volume fw_service_leak
    project_compiler_options)
target_add_sanitizers(fw_service_measurement)
