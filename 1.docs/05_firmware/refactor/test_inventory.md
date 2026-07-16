# Test Inventory: Phase 0 Baseline

**Created**: 2026-07-16
**Total Test Files**: 39 (38 passing, 2 failing in Release)

## Unit Tests (Core — pure logic, no platform deps)

| # | Test Name | File | Category | Domain | Status |
|---|-----------|------|----------|--------|--------|
| 1 | `test_event_queue` | `test_event_queue.c` | Unit | Infrastructure — Event queue FIFO, priority, overflow, coalescing | ✅ PASS |
| 2 | `test_data_repository` | `test_data_repository.c` | Unit | Infrastructure — Data repository double-buffer, atomic publish, provenance guard | ✅ PASS |
| 3 | `test_system_fsm` | `test_system_fsm.c` | Unit | Application — System FSM 6-mode transitions | ✅ PASS |
| 4 | `test_app_event_router` | `test_app_event_router.c` | Unit | Infrastructure — Event router 8 owner ranges + unknown event | ✅ PASS |
| 5 | `test_numeric` | `test_numeric.c` | Unit | Infrastructure — Checked arithmetic, interpolation, overflow | ✅ PASS |

## Unit Tests (Domain — algorithm logic, no platform deps)

| # | Test Name | File | Category | Domain | Status |
|---|-----------|------|----------|--------|--------|
| 6 | `test_temperature` | `test_temperature.c` | Unit | Measurement — Temperature profile interpolation | ✅ PASS |
| 7 | `test_flow` | `test_flow.c` | Unit | Measurement — Flow processing, forward/reverse | ✅ PASS |
| 8 | `test_pressure` | `test_pressure.c` | Unit | Measurement — Pressure processing | ✅ PASS |
| 9 | `test_volume_admission` | `test_volume_admission.c` | Unit | Product — Volume accumulator admission gate | ✅ PASS |
| 10 | `test_volume_arithmetic` | `test_volume_arithmetic.c` | Unit | Product — Volume arithmetic | ❌ FAIL (6/7 assertions, pre-existing) |
| 11 | `test_volume_duplicate` | `test_volume_duplicate.c` | Unit | Product — Volume duplicate identity tracking | ❌ FAIL (2/3 assertions, pre-existing) |
| 12 | `test_volume_reset` | `test_volume_reset.c` | Unit | Product — Volume reset/restore | ✅ PASS |
| 13 | `test_storage_codec` | `test_storage_codec.c` | Unit | Storage — Storage codec round-trip, CRC-32 | ✅ PASS |
| 14 | `test_storage_ab_slots` | `test_storage_ab_slots.c` | Unit | Storage — A/B slot selection | ✅ PASS |
| 15 | `test_leak_config` | `test_leak_config.c` | Unit | Leak — Leak configuration validation | ✅ PASS |
| 16 | `test_leak_tracker` | `test_leak_tracker.c` | Unit | Leak — Leak tracker state | ✅ PASS |
| 17 | `test_leak_state` | `test_leak_state.c` | Unit | Leak — Leak state machine | ✅ PASS |
| 18 | `test_delivery_service` | `test_delivery_service.c` | Unit | Connectivity — DeliveryService state machine | ✅ PASS |
| 19 | `test_telemetry_queue` | `test_telemetry_queue.c` | Unit | Connectivity — TelemetryQueue enqueue/dequeue | ✅ PASS |
| 20 | `test_telemetry_builder` | `test_telemetry_builder.c` | Unit | Connectivity — TelemetryBuilder record build | ✅ PASS |
| 21 | `test_reporting_schedule` | `test_reporting_schedule.c` | Unit | Connectivity — ReportingSchedule window boundary | ✅ PASS |
| 22 | `test_time_service` | `test_time_service.c` | Unit | Connectivity — TimeService holdover expiry | ✅ PASS |

## Platform Unit Tests (Linux-specific backend)

| # | Test Name | File | Category | Domain | Status |
|---|-----------|------|----------|--------|--------|
| 23 | `test_scheduler` | `test_scheduler.c` | Platform Unit | Infrastructure — MonotonicScheduler | ✅ PASS |
| 24 | `test_linux_virtual_clock` | `test_linux_virtual_clock.c` | Platform Unit | Platform — VirtualClock advance/reject | ✅ PASS |
| 25 | `test_linux_action_queue` | `test_linux_action_queue.c` | Platform Unit | Platform — ScheduledActionQueue dispatch | ✅ PASS (⚠️ hangs in Debug w/ASan) |
| 26 | `test_linux_providers` | `test_linux_providers.c` | Platform Unit | Platform — SPI/I2C/GPIO providers | ✅ PASS |
| 27 | `test_linux_peers` | `test_linux_peers.c` | Platform Unit | Platform — MAX35103/ZSSC3241/F-RAM peers | ✅ PASS |

## Simulation Tests

| # | Test Name | File | Category | Domain | Status |
|---|-----------|------|----------|--------|--------|
| 28 | `test_simulation_harness` | `test_simulation_harness.c` | Simulation | Simulation — Harness init/trace/validate | ✅ PASS |
| 29 | `test_determinism` | `test_determinism.c` | Simulation | Simulation — 5× replay byte-identical trace | ✅ PASS |
| 30 | `test_sim_contract` | `test_sim_contract.c` | Simulation | Simulation — SIM-MAN/SIM-LIFE/SIM-DET contracts | ✅ PASS |

## Integration Scenarios

| # | Test Name | File | Category | Domain | Status |
|---|-----------|------|----------|--------|--------|
| 31 | `test_scenarios_core` | `test_scenarios_core.c` | Integration | System — Boot, stale generation, budget | ✅ PASS |
| 32 | `test_scenarios_max` | `test_scenarios_max.c` | Integration | System — MAX35103 EOC, SPI failure, reset | ✅ PASS |
| 33 | `test_scenarios_zssc` | `test_scenarios_zssc.c` | Integration | System — ZSSC3241 EOC, I2C failure, contention | ✅ PASS |
| 34 | `test_scenarios_metadata` | `test_scenarios_metadata.c` | Integration | System — Production eligibility, provenance | ✅ PASS |

## End-to-End / System Tests

| # | Test Name | File | Category | Domain | Status |
|---|-----------|------|----------|--------|--------|
| 35 | `test_volume_e2e` | `test_volume_e2e.c` | E2E | Product — Volume accumulate→encode→decode→restore | ✅ PASS |
| 36 | `test_boot_restore` | `test_boot_restore.c` | Integration | Storage — Boot restore, newest valid selection | ✅ PASS |
| 37 | `test_power_loss` | `test_power_loss.c` | Integration | Storage — Power-loss recovery, corrupt/torn records | ✅ PASS |
| 38 | `test_reporting_e2e` | `test_reporting_e2e.c` | E2E | Connectivity — Schedule→queue→deliver→ACK | ✅ PASS |
| 39 | `test_linux_simulation` | `test_linux_simulation.c` | Integration | System — Full system simulation, boot to normal/error | ✅ PASS |

---

## Known Failures

| Test | Failures | Triage |
|------|----------|--------|
| `test_volume_arithmetic` | 6/7 assertions | Pre-existing — `expected 1000 uL forward`, `expected OK`, `expected REJECTED_TIME (gap)` |
| `test_volume_duplicate` | 2/3 assertions | Pre-existing — `expected DUPLICATE`, `expected OK for new identity` |

## Known Hangs

| Test | Condition | Triage |
|------|-----------|--------|
| `test_linux_action_queue` | Debug build with ASan+UBSan | Pre-existing — hangs under sanitizer instrumentation |

## Summary

| Category | Count | Pass | Fail | Hang |
|----------|-------|------|------|------|
| Unit (Core) | 5 | 5 | 0 | 0 |
| Unit (Domain) | 17 | 15 | 2 | 0 |
| Platform Unit | 5 | 5 | 0 | 1 (Debug only) |
| Simulation | 3 | 3 | 0 | 0 |
| Integration | 4 | 4 | 0 | 0 |
| E2E/System | 5 | 5 | 0 | 0 |
| **Total** | **39** | **37** | **2** | **1** |
