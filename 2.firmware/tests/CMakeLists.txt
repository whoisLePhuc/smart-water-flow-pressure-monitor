function(add_core_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE infrastructure app project_compiler_options)
    target_add_sanitizers(${TEST_NAME})
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# Core unit tests (pure core, no platform backend)
add_core_test(test_event_queue       test_event_queue.c)
add_core_test(test_event_mediator    test_event_mediator.c)
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

# Phase 0 — Characterization tests (baseline behavior documentation)
add_core_test(test_repo_characterization  test_repo_characterization.c)
add_core_test(test_event_characterization test_event_characterization.c)

# Phase 4 — Event mediator acceptance test
add_core_test(test_power_event_registration test_power_event_registration.c)

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

# Flow processing tests
add_executable(test_flow test_flow.c)
target_link_libraries(test_flow PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_flow)
add_test(NAME test_flow COMMAND test_flow)

# Pressure processing tests
add_executable(test_pressure test_pressure.c)
target_link_libraries(test_pressure PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_pressure)
add_test(NAME test_pressure COMMAND test_pressure)
# Phase 10 — Volume tests
add_executable(test_volume_admission test_volume_admission.c)
target_link_libraries(test_volume_admission PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_volume_admission)
add_test(NAME test_volume_admission COMMAND test_volume_admission)

add_executable(test_volume_arithmetic test_volume_arithmetic.c)
target_link_libraries(test_volume_arithmetic PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_volume_arithmetic)
add_test(NAME test_volume_arithmetic COMMAND test_volume_arithmetic)

add_executable(test_volume_duplicate test_volume_duplicate.c)
target_link_libraries(test_volume_duplicate PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_volume_duplicate)
add_test(NAME test_volume_duplicate COMMAND test_volume_duplicate)

add_executable(test_volume_reset test_volume_reset.c)
target_link_libraries(test_volume_reset PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_volume_reset)
add_test(NAME test_volume_reset COMMAND test_volume_reset)

# Phase 10 — Storage codec and A/B tests
add_executable(test_storage_codec test_storage_codec.c)
target_link_libraries(test_storage_codec PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_storage_codec)
add_test(NAME test_storage_codec COMMAND test_storage_codec)

add_executable(test_storage_ab_slots test_storage_ab_slots.c)
target_link_libraries(test_storage_ab_slots PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_storage_ab_slots)
add_test(NAME test_storage_ab_slots COMMAND test_storage_ab_slots)

# Phase 12 — E2E reporting pipeline test
add_executable(test_reporting_e2e test_reporting_e2e.c)
target_link_libraries(test_reporting_e2e PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_reporting_e2e)
add_test(NAME test_reporting_e2e COMMAND test_reporting_e2e)

# Phase 12 — Delivery service test
add_executable(test_delivery_service test_delivery_service.c)
target_link_libraries(test_delivery_service PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_delivery_service)
add_test(NAME test_delivery_service COMMAND test_delivery_service)

# Phase 12 — Telemetry queue test
add_executable(test_telemetry_queue test_telemetry_queue.c)
target_link_libraries(test_telemetry_queue PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_telemetry_queue)
add_test(NAME test_telemetry_queue COMMAND test_telemetry_queue)

# Phase 12 — Telemetry builder test
add_executable(test_telemetry_builder test_telemetry_builder.c)
target_link_libraries(test_telemetry_builder PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_telemetry_builder)
add_test(NAME test_telemetry_builder COMMAND test_telemetry_builder)

# Phase 12 — Reporting schedule test
add_executable(test_reporting_schedule test_reporting_schedule.c)
target_link_libraries(test_reporting_schedule PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_reporting_schedule)
add_test(NAME test_reporting_schedule COMMAND test_reporting_schedule)

# Phase 12 — Time service test
add_executable(test_time_service test_time_service.c)
target_link_libraries(test_time_service PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_time_service)
add_test(NAME test_time_service COMMAND test_time_service)

# Phase 11 — Leak detection tests
add_executable(test_leak_config test_leak_config.c)
target_link_libraries(test_leak_config PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_leak_config)
add_test(NAME test_leak_config COMMAND test_leak_config)

add_executable(test_leak_tracker test_leak_tracker.c)
target_link_libraries(test_leak_tracker PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_leak_tracker)
add_test(NAME test_leak_tracker COMMAND test_leak_tracker)

add_executable(test_leak_state test_leak_state.c)
target_link_libraries(test_leak_state PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_leak_state)
add_test(NAME test_leak_state COMMAND test_leak_state)

# Phase 10 — Volume E2E pipeline test
add_executable(test_volume_e2e test_volume_e2e.c)
target_link_libraries(test_volume_e2e PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_volume_e2e)
add_test(NAME test_volume_e2e COMMAND test_volume_e2e)

# Phase 5 — Boot restore and power-loss tests
add_executable(test_boot_restore test_boot_restore.c)
target_link_libraries(test_boot_restore PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_boot_restore)
add_test(NAME test_boot_restore COMMAND test_boot_restore)

add_executable(test_power_loss test_power_loss.c)
target_link_libraries(test_power_loss PRIVATE services infrastructure project_compiler_options)
target_add_sanitizers(test_power_loss)
add_test(NAME test_power_loss COMMAND test_power_loss)

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
add_executable(structural_baseline_test structural_baseline_test.c)
target_include_directories(structural_baseline_test PRIVATE ${CMAKE_SOURCE_DIR}/src/infrastructure/event ${CMAKE_SOURCE_DIR}/src/infrastructure/repositories ${CMAKE_SOURCE_DIR}/src/app)
target_link_libraries(structural_baseline_test PRIVATE infrastructure app project_compiler_options)
target_add_sanitizers(structural_baseline_test)

# Architecture check — runs shell script to enforce dependency rules
add_test(NAME architecture_check
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_architecture.sh --enforce
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
