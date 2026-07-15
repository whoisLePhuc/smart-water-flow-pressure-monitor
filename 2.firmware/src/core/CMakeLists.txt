add_library(core STATIC
    app_event.c
    app_event_queue.c
    app_event_loop.c
    app_event_router.c
    monotonic_scheduler.c
    system_fsm.c
    mode_guard.c
    data_repository.c
)

target_include_directories(core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/core
)

target_link_libraries(core
    PUBLIC
        project_compiler_options
)

target_add_sanitizers(core)
