# Refactor Invariants: Phase 0 Baseline

**Created**: 2026-07-16
**Baseline Commit**: `780c12b5c3be7362f7d2fbed2741fb290ab46c9d`

## Purpose

This document defines behaviors that **MUST NOT change** during the firmware refactor (Phases 1–11). Each invariant has a verification method. If a PR causes an invariant violation, that PR must be reverted or fixed before merge.

---

## Invariants

### RI-01: Deterministic Simulation Output

**Description**: The same simulation input scenario MUST produce the same output trace (event sequence, state transitions, numerical results) every time. The simulator uses deterministic virtual time and ordered event processing.

**Verification Method**: `test_determinism` (5× replay → byte-identical traces). Golden trace comparison for `core`, `max`, `zssc` scenarios.

**Associated Tests**:
- `tests/test_determinism.c` — 5× replay, byte-identical NormalizedTrace
- `tests/test_sim_contract.c` (SIM-DET-001) — 3× replay determinism
- Golden traces: `core.trace`, `max.trace`, `zssc.trace` — diff comparison

**Scope**: All phases (P1–P11)

---

### RI-02: Event Priority and Overflow Policy

**Description**: Event priority ordering (CRITICAL > MEASUREMENT > SHARED_RESOURCE > CONFIG > BACKGROUND) and overflow behavior MUST remain unchanged. No event should be silently dropped without documented policy.

**Verification Method**: `test_event_queue` — FIFO ordering, priority ordering, overflow backpressure, CRITICAL reservation.

**Associated Tests**:
- `tests/test_event_queue.c` — 8 test cases
- `tests/test_event_characterization.c` (Phase 0) — ordering, priority, overflow, unknown event

**Scope**: All phases (P1–P11), especially Phase 4 (Event Mediator)

---

### RI-03: System FSM and Guard Semantics

**Description**: SystemModeManager behavior (6 modes: INIT/NORMAL/LOW_POWER/SERVICE/RECOVERY/ERROR) and guard context semantics MUST remain unchanged. Same guard inputs → same mode transitions.

**Verification Method**: `test_system_fsm` — all 53 transitions, stale completion rejection, generation checks.

**Associated Tests**:
- `tests/test_system_fsm.c` — FSM unit tests
- `tests/test_scenarios_core.c` — boot to NORMAL, stale generation

**Scope**: All phases (P1–P11)

---

### RI-04: Numerical Output of Measurement Domains

**Description**: Flow, pressure, temperature, leak, and volume numerical output MUST produce identical results for the same inputs. Algorithm logic is not part of the refactor scope.

**Verification Method**: All domain unit tests pass with identical assertion values.

**Associated Tests**:
- `tests/test_flow.c` — Flow forward/reverse processing
- `tests/test_pressure.c` — Pressure processing
- `tests/test_temperature.c` — Temperature calibration
- `tests/test_volume_admission.c`, `test_volume_reset.c` — Volume tests
- `tests/test_leak_config.c`, `test_leak_tracker.c`, `test_leak_state.c` — Leak tests
- `tests/test_numeric.c` — Arithmetic utilities

**Known Baseline Failures** (pre-existing, outside refactor scope):
- `test_volume_arithmetic` — 6/7 assertions fail
- `test_volume_duplicate` — 2/3 assertions fail

**Scope**: All phases (P1–P11)

---

### RI-05: Persistent A/B Slot and Boot Restore

**Description**: Persistent storage A/B slot selection (newest valid → slot A → slot B), CRC-32/ISO-HDLC validation, and boot restore behavior MUST remain unchanged. Torn records rejected. Unknown schemas handled gracefully.

**Verification Method**: `test_storage_ab_slots`, `test_boot_restore`, `test_power_loss`.

**Associated Tests**:
- `tests/test_storage_codec.c` — Round-trip encode/decode, CRC known-answer
- `tests/test_storage_ab_slots.c` — A/B selection: both valid, one valid, both invalid
- `tests/test_boot_restore.c` — Fresh boot, newest valid, anchor preservation
- `tests/test_power_loss.c` — Corrupt slot, torn record, unknown schema

**Scope**: All phases (P1–P11), especially Phase 8 (Protocol Mapping)

---

### RI-06: Telemetry Format Stability (Until Phase 8)

**Description**: Telemetry wire format and storage record format MUST NOT change before Phase 8 (Protocol Mapping). Any format change after Phase 8 must be versioned with backward compatibility.

**Verification Method**: Telemetry builder tests pass. Golden vectors preserved.

**Associated Tests**:
- `tests/test_telemetry_builder.c` — Record build, sequence increment
- `tests/test_telemetry_queue.c` — Enqueue/dequeue, ACK, TTL, duplicate
- `tests/test_reporting_schedule.c` — Window boundary, deduplication
- `tests/test_reporting_e2e.c` — E2E pipeline

**Scope**: Phases 1–7 (LOCKED), Phase 8+ (versioned changes allowed)

---

### RI-07: No Real Sleep in Simulation

**Description**: The Linux simulation MUST NOT introduce real sleep (`sleep()`, `usleep()`, `nanosleep()`). Deterministic virtual time (`LinuxVirtualClock` in DETERMINISTIC mode) is the only time advancement mechanism.

**Verification Method**: Code review. Architecture check forbids `#include <unistd.h>` with `sleep` in simulation source.

**Associated Tests**: No automated test — enforced via architecture check and manual code review.

**Scope**: All phases (P1–P11)

---

## Verification Summary

| Invariant | Automated Test | Architecture Check | Golden Trace |
|-----------|---------------|-------------------|--------------|
| RI-01 Determinism | ✅ `test_determinism` | N/A | ✅ core/max/zssc |
| RI-02 Event Policy | ✅ `test_event_queue` | N/A | ✅ |
| RI-03 FSM Semantics | ✅ `test_system_fsm` | N/A | ✅ |
| RI-04 Numerical Output | ✅ All domain tests | N/A | N/A |
| RI-05 A/B Slot/Boot | ✅ Storage tests | N/A | N/A |
| RI-06 Telemetry | ✅ Telemetry tests | N/A | ✅ (after P8) |
| RI-07 No Real Sleep | Code review | ✅ Check for `<unistd.h>` | N/A |

## Change Process

If a refactoring phase requires violating an invariant:
1. Document the violation with rationale
2. Create an ADR for the exception
3. Get explicit approval before implementation
4. Update this document with the exception and resolution timeline
