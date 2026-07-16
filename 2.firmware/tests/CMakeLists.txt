add_library(project_test_options INTERFACE)
target_compile_options(project_test_options INTERFACE -UNDEBUG)

# Test-only dependency groups. Production targets always declare granular
# dependencies directly; these variables keep the test declarations readable.
set(FW_INFRA_TEST_LIBS
    fw_infra_queue fw_infra_event fw_infra_repository fw_infra_time
    fw_infra_runtime fw_infra_bus fw_infra_numeric)
set(FW_SERVICE_TEST_LIBS
    fw_service_configuration fw_service_measurement fw_service_processing
    fw_service_calibration fw_service_volume fw_service_leak
    fw_service_storage fw_service_connectivity fw_service_time
    fw_protocol_storage fw_protocol_telemetry
    fw_driver_max35103 fw_driver_zssc3241 fw_driver_storage fw_simulation_stubs)

function(add_core_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE ${FW_INFRA_TEST_LIBS} app project_compiler_options project_test_options)
    target_add_sanitizers(${TEST_NAME})
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# Core unit tests (pure core, no platform backend)
add_core_test(test_event_queue       unit/infrastructure/event/test_event_queue.c)
add_core_test(test_data_repository   unit/infrastructure/repositories/test_data_repository.c)

# Event mediator test — uses app_event_loop which depends on platform_linux symbols (monotonic_now_us, system_request_reset)
add_executable(test_event_mediator unit/infrastructure/event/test_event_mediator.c)
target_link_libraries(test_event_mediator PRIVATE ${FW_INFRA_TEST_LIBS} app platform_linux project_compiler_options project_test_options)
target_add_sanitizers(test_event_mediator)
add_test(NAME test_event_mediator COMMAND test_event_mediator)
add_core_test(test_repo_transaction   unit/infrastructure/repositories/test_repo_transaction.c)
add_core_test(test_system_fsm        unit/app/test_system_fsm.c)
add_core_test(test_app_event_router  unit/infrastructure/event/test_app_event_router.c)

# Scheduler test — needs platform_linux for monotonic_now_us
add_executable(test_scheduler unit/infrastructure/time/test_scheduler.c)
target_link_libraries(test_scheduler PRIVATE app ${FW_INFRA_TEST_LIBS} platform_linux project_compiler_options project_test_options)
target_add_sanitizers(test_scheduler)
add_test(NAME test_scheduler COMMAND test_scheduler)

# Virtual clock test
add_executable(test_linux_virtual_clock unit/infrastructure/time/test_linux_virtual_clock.c)
target_link_libraries(test_linux_virtual_clock PRIVATE platform_linux project_compiler_options project_test_options)
target_add_sanitizers(test_linux_virtual_clock)
add_test(NAME test_linux_virtual_clock COMMAND test_linux_virtual_clock)

# Action queue test
add_executable(test_linux_action_queue unit/infrastructure/time/test_linux_action_queue.c)
target_link_libraries(test_linux_action_queue PRIVATE platform_linux project_compiler_options project_test_options)
target_add_sanitizers(test_linux_action_queue)
add_test(NAME test_linux_action_queue COMMAND test_linux_action_queue)

# Provider contract tests
add_executable(test_linux_providers contract/platform/test_linux_providers.c)
target_link_libraries(test_linux_providers PRIVATE platform_linux project_compiler_options project_test_options)
target_add_sanitizers(test_linux_providers)
add_test(NAME test_linux_providers COMMAND test_linux_providers)

# Device peer tests
add_executable(test_linux_peers contract/platform/test_linux_peers.c)
target_link_libraries(test_linux_peers PRIVATE platform_linux project_compiler_options project_test_options)
target_add_sanitizers(test_linux_peers)
add_test(NAME test_linux_peers COMMAND test_linux_peers)

# Simulation harness test
add_executable(test_simulation_harness system/simulation/test_simulation_harness.c)
target_link_libraries(test_simulation_harness PRIVATE simulation platform_linux app ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_simulation_harness)
add_test(NAME test_simulation_harness COMMAND test_simulation_harness)

# Scenario tests
function(add_scenario_test NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_link_libraries(${NAME} PRIVATE simulation fw_simulation_stubs platform_linux app ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
    target_add_sanitizers(${NAME})
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

add_scenario_test(test_scenarios_core     system/simulation/test_scenarios_core.c)
add_scenario_test(test_scenarios_max      system/simulation/test_scenarios_max.c)
add_scenario_test(test_scenarios_zssc     system/simulation/test_scenarios_zssc.c)
add_scenario_test(test_scenarios_metadata system/simulation/test_scenarios_metadata.c)

# Phase 0 — Characterization tests (baseline behavior documentation)
add_core_test(test_event_characterization unit/infrastructure/event/test_event_characterization.c)

# Phase 4 — Event mediator acceptance test
add_core_test(test_power_event_registration contract/architecture/test_power_event_registration.c)

add_executable(test_measurement_manager unit/services/measurement/test_measurement_manager.c)
target_link_libraries(test_measurement_manager PRIVATE fw_service_measurement ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_measurement_manager)
add_test(NAME test_measurement_manager COMMAND test_measurement_manager)

add_executable(test_stm32_adc_adapter contract/platform/test_stm32_adc_adapter.c)
target_link_libraries(test_stm32_adc_adapter PRIVATE fw_adapter_stm32 project_compiler_options project_test_options)
target_add_sanitizers(test_stm32_adc_adapter)
add_test(NAME test_stm32_adc_adapter COMMAND test_stm32_adc_adapter)

# Phase 10 — Power/battery tests
add_executable(test_power_converter unit/services/power/test_power_converter.c)
target_include_directories(test_power_converter PRIVATE ${CMAKE_SOURCE_DIR}/src/services/power)
target_link_libraries(test_power_converter PRIVATE fw_service_power project_compiler_options project_test_options)
target_add_sanitizers(test_power_converter)
add_test(NAME test_power_converter COMMAND test_power_converter)

add_executable(test_power_health unit/services/power/test_power_health.c)
target_include_directories(test_power_health PRIVATE ${CMAKE_SOURCE_DIR}/src/services/power ${CMAKE_SOURCE_DIR}/src/domain/power)
target_link_libraries(test_power_health PRIVATE fw_service_power fw_domain_power project_compiler_options project_test_options)
target_add_sanitizers(test_power_health)
add_test(NAME test_power_health COMMAND test_power_health)

add_executable(test_power_e2e integration/power/test_power_e2e.c)
target_include_directories(test_power_e2e PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/linux/adapters)
target_link_libraries(test_power_e2e PRIVATE fw_adapter_linux fw_facade_power fw_service_connectivity ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_power_e2e)
add_test(NAME test_power_e2e COMMAND test_power_e2e)

# Port contract tests
add_executable(test_port_contract contract/ports/test_port_contract.c)
target_include_directories(test_port_contract PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/linux/adapters)
target_link_libraries(test_port_contract PRIVATE fw_adapter_linux fw_ports ${FW_INFRA_TEST_LIBS} app project_compiler_options project_test_options)
target_add_sanitizers(test_port_contract)
add_test(NAME test_port_contract COMMAND test_port_contract)

# Numeric utility tests
add_executable(test_numeric unit/infrastructure/numeric/test_numeric.c)
target_include_directories(test_numeric PRIVATE ${CMAKE_SOURCE_DIR}/src/infrastructure/numeric)
target_link_libraries(test_numeric PRIVATE ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_numeric)
add_test(NAME test_numeric COMMAND test_numeric)

# Temperature calibration tests
add_executable(test_temperature unit/services/calibration/test_temperature.c)
target_link_libraries(test_temperature PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_temperature)
add_test(NAME test_temperature COMMAND test_temperature)

# Flow processing tests
add_executable(test_flow unit/services/processing/test_flow.c)
target_link_libraries(test_flow PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_flow)
add_test(NAME test_flow COMMAND test_flow)

# Pressure processing tests
add_executable(test_pressure unit/services/processing/test_pressure.c)
target_link_libraries(test_pressure PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_pressure)
add_test(NAME test_pressure COMMAND test_pressure)
# Phase 10 — Volume tests
add_executable(test_volume_admission unit/services/volume/test_volume_admission.c)
target_link_libraries(test_volume_admission PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_volume_admission)
add_test(NAME test_volume_admission COMMAND test_volume_admission)

add_executable(test_volume_arithmetic unit/services/volume/test_volume_arithmetic.c)
target_link_libraries(test_volume_arithmetic PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_volume_arithmetic)
add_test(NAME test_volume_arithmetic COMMAND test_volume_arithmetic)

add_executable(test_volume_duplicate unit/services/volume/test_volume_duplicate.c)
target_link_libraries(test_volume_duplicate PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_volume_duplicate)
add_test(NAME test_volume_duplicate COMMAND test_volume_duplicate)

add_executable(test_volume_reset unit/services/volume/test_volume_reset.c)
target_link_libraries(test_volume_reset PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_volume_reset)
add_test(NAME test_volume_reset COMMAND test_volume_reset)

# Phase 10 — Storage codec and A/B tests
add_executable(test_storage_codec unit/protocols/storage/test_storage_codec.c)
target_link_libraries(test_storage_codec PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_storage_codec)
add_test(NAME test_storage_codec COMMAND test_storage_codec)

add_executable(test_storage_ab_slots unit/protocols/storage/test_storage_ab_slots.c)
target_link_libraries(test_storage_ab_slots PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_storage_ab_slots)
add_test(NAME test_storage_ab_slots COMMAND test_storage_ab_slots)

add_executable(test_storage_service unit/services/storage/test_storage_service.c)
target_link_libraries(test_storage_service PRIVATE fw_service_storage project_compiler_options project_test_options)
target_add_sanitizers(test_storage_service)
add_test(NAME test_storage_service COMMAND test_storage_service)

# Phase 12 — E2E reporting pipeline test
add_executable(test_reporting_e2e integration/reporting/test_reporting_e2e.c)
target_link_libraries(test_reporting_e2e PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_reporting_e2e)
add_test(NAME test_reporting_e2e COMMAND test_reporting_e2e)

# Phase 12 — Delivery service test
add_executable(test_delivery_service unit/services/connectivity/test_delivery_service.c)
target_link_libraries(test_delivery_service PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_delivery_service)
add_test(NAME test_delivery_service COMMAND test_delivery_service)

# Phase 12 — Telemetry queue test
add_executable(test_telemetry_queue unit/services/connectivity/test_telemetry_queue.c)
target_link_libraries(test_telemetry_queue PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_telemetry_queue)
add_test(NAME test_telemetry_queue COMMAND test_telemetry_queue)

# Phase 12 — Telemetry builder test
add_executable(test_telemetry_builder unit/protocols/telemetry/test_telemetry_builder.c)
target_link_libraries(test_telemetry_builder PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_telemetry_builder)
add_test(NAME test_telemetry_builder COMMAND test_telemetry_builder)

# Phase 12 — Reporting schedule test
add_executable(test_reporting_schedule unit/services/connectivity/test_reporting_schedule.c)
target_link_libraries(test_reporting_schedule PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_reporting_schedule)
add_test(NAME test_reporting_schedule COMMAND test_reporting_schedule)

# Phase 12 — Time service test
add_executable(test_time_service unit/services/time/test_time_service.c)
target_link_libraries(test_time_service PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_time_service)
add_test(NAME test_time_service COMMAND test_time_service)

# Phase 11 — Leak detection tests
add_executable(test_leak_config unit/services/leak/test_leak_config.c)
target_link_libraries(test_leak_config PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_leak_config)
add_test(NAME test_leak_config COMMAND test_leak_config)

add_executable(test_leak_tracker unit/services/leak/test_leak_tracker.c)
target_link_libraries(test_leak_tracker PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_leak_tracker)
add_test(NAME test_leak_tracker COMMAND test_leak_tracker)

add_executable(test_leak_state unit/services/leak/test_leak_state.c)
target_link_libraries(test_leak_state PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_leak_state)
add_test(NAME test_leak_state COMMAND test_leak_state)

# Phase 10 — Volume E2E pipeline test
add_executable(test_volume_e2e integration/volume/test_volume_e2e.c)
target_link_libraries(test_volume_e2e PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_volume_e2e)
add_test(NAME test_volume_e2e COMMAND test_volume_e2e)

# Phase 5 — Boot restore and power-loss tests
add_executable(test_boot_restore integration/storage/test_boot_restore.c)
target_link_libraries(test_boot_restore PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_boot_restore)
add_test(NAME test_boot_restore COMMAND test_boot_restore)

add_executable(test_power_loss integration/storage/test_power_loss.c)
target_link_libraries(test_power_loss PRIVATE ${FW_SERVICE_TEST_LIBS} ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_power_loss)
add_test(NAME test_power_loss COMMAND test_power_loss)

# Determinism gate
add_executable(test_determinism system/simulation/test_determinism.c)
target_link_libraries(test_determinism PRIVATE simulation platform_linux app ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_determinism)
add_test(NAME test_determinism COMMAND test_determinism)

# Simulator contract tests (8B safety net)
add_executable(test_sim_contract system/simulation/test_sim_contract.c)
target_link_libraries(test_sim_contract PRIVATE simulation platform_linux app ${FW_INFRA_TEST_LIBS} project_compiler_options project_test_options)
target_add_sanitizers(test_sim_contract)
add_test(NAME test_sim_contract COMMAND test_sim_contract)


# Integration test — uses platform backend
add_executable(test_linux_simulation integration/platform/test_linux_simulation.c)
target_link_libraries(test_linux_simulation PRIVATE app ${FW_INFRA_TEST_LIBS} platform_linux project_compiler_options project_test_options)
target_add_sanitizers(test_linux_simulation)
add_test(NAME test_linux_simulation COMMAND test_linux_simulation)
add_executable(structural_baseline_test contract/architecture/structural_baseline_test.c)
target_include_directories(structural_baseline_test PRIVATE ${CMAKE_SOURCE_DIR}/src/infrastructure/event ${CMAKE_SOURCE_DIR}/src/infrastructure/repositories ${CMAKE_SOURCE_DIR}/src/app)
target_link_libraries(structural_baseline_test PRIVATE ${FW_INFRA_TEST_LIBS} app project_compiler_options project_test_options)
target_add_sanitizers(structural_baseline_test)

# Architecture check — runs shell script to enforce dependency rules
add_test(NAME architecture_check
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_architecture.sh --enforce
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
