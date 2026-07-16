# ── Granular infrastructure sub-targets ──

add_library(fw_infra_queue STATIC queues/app_event_queue.c)
target_include_directories(fw_infra_queue PUBLIC
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fw_infra_queue PUBLIC core project_compiler_options)
target_add_sanitizers(fw_infra_queue)

add_library(fw_infra_event STATIC
    event/app_event.c
    event/event_mediator.c
)
target_include_directories(fw_infra_event
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/event
)
target_link_libraries(fw_infra_event
    PUBLIC
        core
        fw_infra_queue
        project_compiler_options
)
target_add_sanitizers(fw_infra_event)

add_library(fw_infra_repository STATIC
    repositories/data_repository.c
    repositories/repo_transaction.c
)
target_include_directories(fw_infra_repository
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
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
        ${CMAKE_CURRENT_SOURCE_DIR}
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

add_library(fw_infra_runtime STATIC
    event/app_event_loop.c
    event/app_event_router.c
)
target_include_directories(fw_infra_runtime PUBLIC
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fw_infra_runtime PUBLIC
    fw_infra_event fw_infra_repository fw_infra_time core
    project_compiler_options)
target_add_sanitizers(fw_infra_runtime)

add_library(fw_infra_bus STATIC
    bus/i2c_bus_manager.c
)
target_include_directories(fw_infra_bus PUBLIC
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/bus)
target_link_libraries(fw_infra_bus PUBLIC fw_infra_event core project_compiler_options)
target_add_sanitizers(fw_infra_bus)

add_library(fw_infra_numeric STATIC
    numeric/checked_math.c
    numeric/interpolation.c
)
target_include_directories(fw_infra_numeric PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/numeric
)
target_link_libraries(fw_infra_numeric PUBLIC core project_compiler_options)
target_add_sanitizers(fw_infra_numeric)
