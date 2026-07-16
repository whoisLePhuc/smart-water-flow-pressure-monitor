# ── Granular infrastructure sub-targets ──

add_library(fw_infra_event STATIC
    event/app_event.c
    event/app_event_queue.c
)
target_include_directories(fw_infra_event
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/event
)
target_link_libraries(fw_infra_event
    PUBLIC
        core
        project_compiler_options
)
target_add_sanitizers(fw_infra_event)

add_library(fw_infra_repository STATIC
    repositories/data_repository.c
)
target_include_directories(fw_infra_repository
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/event
        ${CMAKE_CURRENT_SOURCE_DIR}/repositories
)
target_link_libraries(fw_infra_repository
    PUBLIC
        core
        project_compiler_options
)
target_add_sanitizers(fw_infra_repository)

add_library(fw_infra_time STATIC
    time/monotonic_scheduler.c
)
target_include_directories(fw_infra_time
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/event
        ${CMAKE_CURRENT_SOURCE_DIR}/time
)
target_link_libraries(fw_infra_time
    PUBLIC
        core
        project_compiler_options
)
target_add_sanitizers(fw_infra_time)

# ── Compatibility target (kept for migration, removed in Phase 9) ──
add_library(infrastructure STATIC
    event/app_event_loop.c
    event/app_event_router.c
    bus/i2c_bus_manager.c
    numeric/checked_math.c
    numeric/interpolation.c
    numeric/sensor_profile.c
)
target_include_directories(infrastructure PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/bus
    ${CMAKE_CURRENT_SOURCE_DIR}/numeric
)
target_link_libraries(infrastructure
    PUBLIC
        fw_infra_event
        fw_infra_repository
        fw_infra_time
        core
        firmware_includes
        project_compiler_options
)
target_add_sanitizers(infrastructure)
