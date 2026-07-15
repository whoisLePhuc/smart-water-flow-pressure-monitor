add_library(infrastructure STATIC
    event/app_event.c
    event/app_event_queue.c
    event/app_event_loop.c
    event/app_event_router.c
    bus/i2c_bus_manager.c
    repositories/data_repository.c
    time/monotonic_scheduler.c
    numeric/checked_math.c
    numeric/interpolation.c
    numeric/sensor_profile.c
)

target_include_directories(infrastructure PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/event
    ${CMAKE_CURRENT_SOURCE_DIR}/bus
    ${CMAKE_CURRENT_SOURCE_DIR}/repositories
    ${CMAKE_CURRENT_SOURCE_DIR}/time
)

target_link_libraries(infrastructure
    PUBLIC
        core
        firmware_includes
        project_compiler_options
)

target_add_sanitizers(infrastructure)
