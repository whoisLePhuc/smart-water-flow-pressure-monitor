add_library(app STATIC
    app_composition.c
)

target_include_directories(app
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(app
    PUBLIC
        fw_infra_event
        fw_infra_repository
        fw_infra_time
        fw_infra_runtime
        fw_service_measurement
        fw_ports
        fw_facade_measurement
        fw_facade_storage
        fw_facade_connectivity
        fw_facade_power
        project_compiler_options
)
