function(add_core_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE infrastructure app project_compiler_options)
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
target_link_libraries(test_scheduler PRIVATE app infrastructure platform_linux project_compiler_options)
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
target_link_libraries(test_simulation_harness PRIVATE simulation platform_linux app infrastructure project_compiler_options)
target_add_sanitizers(test_simulation_harness)
add_test(NAME test_simulation_harness COMMAND test_simulation_harness)

# Scenario tests
function(add_scenario_test NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_link_libraries(${NAME} PRIVATE simulation platform_linux app infrastructure project_compiler_options)
    target_add_sanitizers(${NAME})
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

add_scenario_test(test_scenarios_core     test_scenarios_core.c)
add_scenario_test(test_scenarios_max      test_scenarios_max.c)
add_scenario_test(test_scenarios_zssc     test_scenarios_zssc.c)
add_scenario_test(test_scenarios_metadata test_scenarios_metadata.c)

# Numeric utility tests
add_executable(test_numeric test_numeric.c)
target_include_directories(test_numeric PRIVATE ${CMAKE_SOURCE_DIR}/src/infrastructure/numeric)
target_link_libraries(test_numeric PRIVATE infrastructure project_compiler_options)
target_add_sanitizers(test_numeric)
add_test(NAME test_numeric COMMAND test_numeric)

# Temperature calibration tests
add_executable(test_temperature test_temperature.c)
target_link_libraries(test_temperature PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_temperature)
add_test(NAME test_temperature COMMAND test_temperature)
# Determinism gate
add_executable(test_determinism test_determinism.c)
target_link_libraries(test_determinism PRIVATE simulation platform_linux app infrastructure project_compiler_options)
target_add_sanitizers(test_determinism)
add_test(NAME test_determinism COMMAND test_determinism)

# Simulator contract tests (8B safety net)
add_executable(test_sim_contract test_sim_contract.c)
target_link_libraries(test_sim_contract PRIVATE simulation platform_linux app infrastructure project_compiler_options)
target_add_sanitizers(test_sim_contract)
add_test(NAME test_sim_contract COMMAND test_sim_contract)


# Integration test — uses platform backend
add_executable(test_linux_simulation test_linux_simulation.c)
target_link_libraries(test_linux_simulation PRIVATE app infrastructure platform_linux project_compiler_options)
target_add_sanitizers(test_linux_simulation)
add_test(NAME test_linux_simulation COMMAND test_linux_simulation)
