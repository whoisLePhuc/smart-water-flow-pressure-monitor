function(add_core_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE core project_compiler_options)
    target_add_sanitizers(${TEST_NAME})
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# Core unit tests (pure core, no platform backend)
add_core_test(test_event_queue       test_event_queue.c)
add_core_test(test_data_repository   test_data_repository.c)
add_core_test(test_system_fsm        test_system_fsm.c)
add_core_test(test_app_event_router  test_app_event_router.c)

# Scheduler test — needs platform_linux for monotonic_now_us
add_executable(test_scheduler test_scheduler.c)
target_link_libraries(test_scheduler PRIVATE core platform_linux project_compiler_options)
target_add_sanitizers(test_scheduler)
add_test(NAME test_scheduler COMMAND test_scheduler)

# Virtual clock test
add_executable(test_linux_virtual_clock test_linux_virtual_clock.c)
target_link_libraries(test_linux_virtual_clock PRIVATE platform_linux project_compiler_options)
target_add_sanitizers(test_linux_virtual_clock)
add_test(NAME test_linux_virtual_clock COMMAND test_linux_virtual_clock)

# Action queue test
add_executable(test_linux_action_queue test_linux_action_queue.c)
target_link_libraries(test_linux_action_queue PRIVATE platform_linux project_compiler_options)
target_add_sanitizers(test_linux_action_queue)
add_test(NAME test_linux_action_queue COMMAND test_linux_action_queue)

# Provider contract tests
add_executable(test_linux_providers test_linux_providers.c)
target_link_libraries(test_linux_providers PRIVATE platform_linux project_compiler_options)
target_add_sanitizers(test_linux_providers)
add_test(NAME test_linux_providers COMMAND test_linux_providers)

# Device peer tests
add_executable(test_linux_peers test_linux_peers.c)
target_link_libraries(test_linux_peers PRIVATE platform_linux project_compiler_options)
target_add_sanitizers(test_linux_peers)
add_test(NAME test_linux_peers COMMAND test_linux_peers)

# Simulation harness test
add_executable(test_simulation_harness test_simulation_harness.c)
target_link_libraries(test_simulation_harness
    PRIVATE
        simulation
        platform_linux
        project_compiler_options
)
# Use start-group/end-group to resolve cross-library references
target_link_options(test_simulation_harness PRIVATE
    -Wl,--start-group
    $<TARGET_FILE:core>
    $<TARGET_FILE:platform_linux>
    -Wl,--end-group
)
target_add_sanitizers(test_simulation_harness)
add_test(NAME test_simulation_harness COMMAND test_simulation_harness)

# Integration test — uses platform backend
add_executable(test_linux_simulation test_linux_simulation.c)
target_link_libraries(test_linux_simulation PRIVATE core platform_linux project_compiler_options)
target_add_sanitizers(test_linux_simulation)
add_test(NAME test_linux_simulation COMMAND test_linux_simulation)
