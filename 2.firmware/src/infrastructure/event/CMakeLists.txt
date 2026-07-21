# fw_infra_event — core event types and mediator
add_library(fw_infra_event STATIC
    app_event.c
    event_mediator.c
)
target_include_directories(fw_infra_event PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(fw_infra_event PUBLIC core fw_infra_queue project_compiler_options)
target_add_sanitizers(fw_infra_event)

# fw_infra_runtime — event loop, router, action executor
add_library(fw_infra_runtime STATIC
    app_event_loop.c
    app_event_router.c
    fsm_action_executor.c
)
target_include_directories(fw_infra_runtime PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(fw_infra_runtime PUBLIC
    fw_infra_event fw_infra_repository fw_infra_time core project_compiler_options)
target_add_sanitizers(fw_infra_runtime)
