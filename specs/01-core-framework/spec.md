# Feature Specification: Phase 1 — Core Framework

**Feature Branch**: `01-core-framework`

**Created**: 2026-07-14

**Status**: Draft

**Input**: Firmware architecture docs (FW-CORE-001–005), reviewed architecture

## User Scenarios & Testing

This is an infrastructure phase — no end-user stories. The "users" are
downstream firmware modules (MeasurementManager, BleConfigService, etc.) and
the developer writing integration tests. Each module acts as an independently
testable unit.

### Module 1 — Event Queue (Priority: P1)

**What**: Bounded event queue with 5 delivery classes, priority ordering,
overflow policy, ISR-safe ingress, stale/duplicate rejection.

**Why P1**: Every other module depends on event delivery.

**Independent Test**: Post events with different priorities/classes, verify
ordering, overflow behavior, and stale rejection — all in isolation.

### Module 2 — Monotonic Scheduler (Priority: P1)

**What**: Anchored periodic and one-shot job scheduling with virtual clock.

**Why P1**: Measurement and timeout scheduling depend on this.

**Independent Test**: Schedule jobs, advance virtual time, verify deadlines
fire at correct anchor + N×period with no drift.

### Module 3 — Data Repository (Priority: P1)

**What**: Double-buffer RuntimeSnapshot with C11 atomic active-index swap.

**Why P1**: All measurement results flow through this to LCD, telemetry, BLE.

**Independent Test**: Publish results, acquire read handles, verify atomic
swap and no mixed-version reads.

### Module 4 — System FSM (Priority: P1)

**What**: 6-mode FSM with 53 transition entries, guard-based evaluation,
mode admission matrix.

**Why P1**: System behavior and mode transitions are foundational.

**Independent Test**: Inject events with explicit ModeGuardContext, verify
correct transition, guard rejection, invariant enforcement.

### Module 5 — Event Loop (Priority: P1)

**What**: Cooperative dispatch loop: collect → bounded work → publish →
low-power check.

**Why P1**: Ties all core modules together into a runnable system.

**Independent Test**: Run with fake platform ports, verify event processing
order and work budget.

### Module 6 — Linux Platform Backend (Priority: P1)

**What**: Virtual clock, system control, platform runtime for Linux simulation.

**Why P1**: Enables deterministic testing before STM32 hardware.

**Independent Test**: Advance virtual time, inject events, verify end-to-end
scenario.

### Module 7 — Linux Simulator App (Priority: P2)

**What**: Runnable executable demonstrating boot → normal → low-power → wake.

**Why P2**: Integration demo; unit tests cover correctness.

**Independent Test**: Run the simulator, verify console output matches
expected state sequence.

## Requirements

### Functional Requirements

- **FR-001**: Event queue MUST support at least 5 delivery classes.
- **FR-002**: Event queue MUST reject stale events by generation mismatch.
- **FR-003**: Critical events MUST NOT be silently dropped on overflow.
- **FR-004**: Scheduler MUST support anchored periodic deadlines.
- **FR-005**: Scheduler MUST NOT drift: deadline(n) = anchor + n × period.
- **FR-006**: Scheduler MUST use virtual monotonic time, not wall clock.
- **FR-007**: Data Repository MUST use exactly 2 snapshot buffers.
- **FR-008**: Snapshot publication MUST use atomic active-index swap.
- **FR-009**: Each accepted source event MUST publish at most 1 final snapshot.
- **FR-010**: FSM MUST have 6 modes: INIT, NORMAL, LOW_POWER, SERVICE, RECOVERY, ERROR.
- **FR-011**: FSM MUST evaluate guards from explicit ModeGuardContext.
- **FR-012**: FSM MUST NOT allow direct ERROR → NORMAL transition.
- **FR-013**: Event loop MUST process critical events before low-power/reporting.
- **FR-014**: Event loop MUST bound work per turn (exact budget TBD).
- **FR-015**: Core modules MUST NOT include POSIX, STM32 HAL, or Linux headers.
- **FR-016**: Virtual clock MUST support Now(), AdvanceBy(), AdvanceTo().

## Success Criteria

- All acceptance criteria from plan.md pass.
- No core module links against POSIX or STM32 HAL.
- All tests pass with address sanitizer and undefined behavior sanitizer.
- Linux simulator runs boot → NORMAL → LOW_POWER → WAKE → NORMAL.
- CTest reports 100% pass rate.
