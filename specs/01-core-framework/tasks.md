---
description: "Task list for Phase 1 — Core Framework implementation"
---

# Tasks: Phase 1 — Core Framework

**Input**: `specs/01-core-framework/plan.md`, `specs/01-core-framework/spec.md`

**Prerequisites**: plan.md (required), spec.md (required)

**Tests**: Test tasks ARE included — each module phase includes unit tests.

**Organization**: Tasks are organized by build order (bottom-up dependency).
Each phase produces independently testable module(s).

---

## Phase 1: Setup (Build System + Directory Structure)

**Purpose**: Project initialization and CMake build infrastructure

- [x] T001 Create `3.firmware/` directory tree:
  `include/core/`, `include/platform/`, `src/core/`,
  `src/platform/linux/`, `apps/linux_sim/`, `tests/`, `cmake/`
- [x] T002 [P] Create `3.firmware/cmake/warnings.cmake` — compiler warning flags
  (`-Wall -Wextra -Werror -Wpedantic`) and sanitizer helper
- [x] T003 Create `3.firmware/CMakeLists.txt` — top-level project config (C11,
  CTest, subdirectories for core static lib, linux_sim app, tests)

**Checkpoint**: `cmake -B build && cmake --build build` succeeds (empty targets)

---

## Phase 2: Foundational Types (Headers Only)

**Purpose**: Shared type definitions that all modules depend on — no `.c` files

### Data Model

- [x] T004 Create `3.firmware/include/core/data_model.h` — canonical types:
  `ResultMetadata` (validity/freshness/acceptance/provenance/time_quality),
  `TemperatureResult`, `FlowResult`, `PressureResult`, `VolumeState`,
  `LeakDetectionResult`, `LeakState`, `LeakEvaluationStatus`,
  `SystemMode` enum (6 modes), `SystemModeContext`, `ModeGuardContext`,
  `RuntimeSnapshot`, `DataValidity`, `DataFreshness`, `ProductionAcceptance`,
  `DataProvenance`, `EventIds` enum (`EVT_*` catalog from FW-CORE-003 §17.1)

### Platform Ports

- [x] T005 [P] Create `3.firmware/include/platform/monotonic_clock_port.h` —
  `monotonic_now_us(void)` function pointer or weak symbol
- [x] T006 [P] Create `3.firmware/include/platform/system_control_port.h` —
  `system_request_reset(uint32_t reason)` function pointer or weak symbol
- [x] T007 [P] Create `3.firmware/include/platform/platform_runtime.h` —
  `platform_init(void)`, `platform_poll(void)` function pointers or weak symbols

**Checkpoint**: All headers compile standalone via `#include` test

---

## Phase 3: Event Subsystem

**Purpose**: Event envelope, queue, and helpers — foundation for all inter-module communication

- [x] T008 [P] Create `3.firmware/include/core/app_event_queue.h` — `AppEvent_t`
  envelope, `AppEventDelivery` enum (5 classes), `EventPostResult` enum,
  `app_event_queue_init()`, `app_event_queue_post()`,
  `app_event_queue_post_from_isr()`, `app_event_queue_try_get()`,
  `app_event_queue_get_count()`, `app_event_queue_get_overflow_count()`
- [x] T009 [P] Create `3.firmware/include/core/app_event.h` — event helper
  functions: `app_event_is_stale()`, `app_event_match_correlation()`,
  `app_event_make_completion()`, `app_event_make_deadline()`
- [x] T010 Create `3.firmware/src/core/app_event_queue.c` — bounded ring buffer
  implementation with per-class overflow policy: critical → emergency flag,
  measurement → reserved slot, config → backpressure, LCD → coalesce
- [x] T011 Create `3.firmware/src/core/app_event.c` — event helper implementations
- [x] T012 [P] Write `3.firmware/tests/test_event_queue.c` — test delivery class
  ordering, same-priority FIFO, priority inversion protection, overflow
  backpressure, ISR-safe post, duplicate rejection, stale generation rejection,
  empty queue behavior, queue full + critical escalation

**Checkpoint**: `test_event_queue` passes, no sanitizer violations

---

## Phase 4: Monotonic Scheduler

**Purpose**: Anchored periodic and one-shot scheduling with virtual clock

- [x] T013 Create `3.firmware/include/core/scheduler.h` — `SchedulerJob`,
  `SchedulerJobId`, `MissPolicy`, `ScheduleResult`,
  `scheduler_init()`, `scheduler_schedule_one_shot()`,
  `scheduler_schedule_periodic()`, `scheduler_cancel()`,
  `scheduler_dispatch_due(now_us)`, `scheduler_get_next_deadline()`
- [x] T014 Create `3.firmware/src/core/monotonic_scheduler.c` — deadline list
  management, anchored reschedule (`deadline(n) = anchor + n × period`),
  missed deadline policy (no burst catch-up), generation-based cancel,
  earliest deadline query
- [x] T015 [P] Write `3.firmware/tests/test_scheduler.c` — one-shot deadline,
  periodic anchor tracking (no drift), missed multiple periods (no burst),
  cancel by generation, invalid period rejection, empty scheduler,
  wrap-safe deadline comparison, `scheduler_get_next_deadline()`

**Checkpoint**: `test_scheduler` passes, no real `sleep()` in test

---

## Phase 5: Data Repository

**Purpose**: Double-buffer RuntimeSnapshot with C11 atomic swap

- [x] T016 Create `3.firmware/include/core/data_repository.h` — `DataRepository`
  opaque struct, `DataPublishResult`, `SnapshotReadHandle`,
  `data_repository_init()`, `data_repository_accept_flow()`,
  `data_repository_accept_pressure()`, `data_repository_accept_temperature()`,
  `data_repository_accept_volume()`, `data_repository_accept_leak()`,
  `data_repository_accept_mode()`,
  `data_repository_snapshot_acquire()` (capture-once),
  `snapshot_read_ptr()`, `data_repository_snapshot_release()`,
  `data_repository_publish_if_requested()`
- [x] T017 Create `3.firmware/src/core/data_repository.c` — two `RuntimeSnapshot`
  buffers, `atomic_uint_fast8_t active_index`, writer builds inactive buffer
  then `atomic_store_explicit(..., memory_order_release)`, reader `atomic_load_explicit(..., memory_order_acquire)`,
  one-final-snapshot-per-turn guard (FW-DATA-REQ-016)
- [x] T018 Write `3.firmware/tests/test_data_repository.c` — writer only modifies
  inactive buffer, atomic swap visible to reader, one source event → max one
  snapshot per turn, consumer capture-once consistency, concurrent reader/writer
  stress (no mixed version), stale result rejection, provenance guard
  (non-production not accepted)

**Checkpoint**: `test_data_repository` passes, atomic semantics verified

---

## Phase 6: System FSM

**Purpose**: Mode guard provider and 6-mode FSM with 53 transition entries

### Mode Guard

- [x] T019 Create `3.firmware/include/core/mode_guard.h` — `ModeGuardProvider`
  opaque struct, `mode_guard_init()`, `mode_guard_capture(event, current_mode)`,
  `mode_guard_get_context()` — reads evidence from `DataRepository` and
  diagnostics to populate `ModeGuardContext`
- [x] T020 Create `3.firmware/src/core/mode_guard.c` — capture published
  status/readiness evidence, build immutable guard snapshot, validate
  generation consistency across guard fields
- [x] T021 [P] Write `3.firmware/tests/test_mode_guard.c` — core_ready true/false,
  flow_readiness_evidence_valid toggle, blocker_mask propagation, guard
  generation consistency check

### System FSM

- [x] T022 Create `3.firmware/include/core/system_fsm.h` — `SystemModeManager`
  opaque struct, `FsmDispatchResult` enum (7 outcomes),
  `system_fsm_init()`, `system_fsm_dispatch(manager, event, guards)`,
  `system_fsm_get_context()`, `system_fsm_get_transition_record()`
- [x] T023 Create `3.firmware/src/core/system_fsm.c` — const transition table
  with all 53 `TR-SYS-*` entries (FW-CORE-004 §9.3–9.8), table-driven
  dispatch, guard evaluation, action token enqueue, mode generation
  increment, transition record capture, unhandled-event policy matrix,
  invariant fault detection (invalid enum, forbidden ERROR→NORMAL,
  non-owner commit)
- [x] T024 Write `3.firmware/tests/test_system_fsm.c` — each TR-SYS-* guard true
  → correct next mode; guard false → stay + correct dispatch result;
  ERROR→NORMAL forbidden; event with no transition → policy applied;
  critical event beats low-power; duplicate/stale event → no duplicate
  entry/exit; invariant fault detection; sequence/generation increment

**Checkpoint**: `test_mode_guard` + `test_system_fsm` pass, all 53 transitions testable

---

## Phase 7: Event Loop

**Purpose**: Cooperative dispatch loop integrating all core modules

- [x] T025 Create `3.firmware/include/core/app_event_loop.h` — `AppEventLoop`
  opaque struct, `LoopConfig` (budget settings), `app_event_loop_init()`,
  `app_event_loop_run_once()`, `app_event_loop_collect_events()`,
  `app_event_loop_run_ready_work()`, `app_event_loop_is_idle()`
- [x] T026 Create `3.firmware/src/core/app_event_loop.c` — collect events from
  queue, select by priority/fairness, dispatch FSM step, run service
  bounded steps, publish final snapshot when requested, update health
  evidence, evaluate low-power blockers, per-turn budget enforcement
- [x] T027 Write `3.firmware/tests/test_event_loop.c` — collect → dispatch →
  publish sequence, priority ordering under mixed load, budget exhaustion
  (work continues next turn), idle detection, no starvation of low-priority
  work, snapshot publication atomicity

**Checkpoint**: `test_event_loop` passes, full dispatch cycle verified

---

## Phase 8: Linux Platform Backend

**Purpose**: Virtual clock, system control, and platform runtime for Linux

- [x] T028 Create `3.firmware/src/platform/linux/virtual_clock.c` — `monotonic_now_us()`:
  virtual mode (advance on demand) and real mode (`clock_gettime`),
  `virtual_clock_set(now_us)`, `virtual_clock_advance(delta_us)`,
  `virtual_clock_set_mode(VIRTUAL/REAL)`
- [x] T029 Create `3.firmware/src/platform/linux/linux_system_control.c` —
  `system_request_reset()`: capture reason, log, `exit()` in simulation mode
- [x] T030 Create `3.firmware/src/platform/linux/linux_platform_runtime.c` —
  `platform_init()`, `platform_poll()`: stdin event injection stub,
  default implementations that bind to virtual clock + exit-on-reset

**Checkpoint**: Linux platform backend compiles and links with core

---

## Phase 9: Linux Simulator App

**Purpose**: Runnable executable demonstrating end-to-end core framework

- [x] T031 Create `3.firmware/apps/linux_sim/main.c` — instantiate all core
  modules, run event loop with virtual clock, inject scenario:
  init → NORMAL → low-power request (blocked by storage) → resolve blocker →
  LOW_POWER → WAKE → NORMAL → CRITICAL_ERROR → ERROR, print state transitions
- [x] T032 Write `3.firmware/tests/test_linux_simulation.c` — scenario-based
  integration test using quickstart.md scenarios: boot success, boot recovery,
  boot error, normal→low-power→wake, blocked low-power, critical preempts
  low-power, snapshot consistency end-to-end

**Checkpoint**: `test_linux_simulation` passes, `linux_sim` runs with visible
state machine output

---

## Phase 10: Polish & Cross-Cutting

**Purpose**: CMake integration, CI readiness, documentation

- [x] T033 Update `3.firmware/CMakeLists.txt` — add `add_test()` for all test
  targets, `enable_testing()`, CTest configuration
- [x] T034 Add sanitizer integration: `cmake/warnings.cmake` — auto-detect
  `-fsanitize=address,undefined` for Debug builds, exclusion for STM32 cross-compile
- [x] T035 Create `3.firmware/tests/CMakeLists.txt` — build all test executables,
  link against core static lib + fake/mock clock port
- [x] T036 Verify complete build: `cmake -B build && cmake --build build` with
  `-DCMAKE_BUILD_TYPE=Debug` and sanitizer flags
- [x] T037 Run full test suite: `cd build && ctest --output-on-failure` —
  all tests pass, no sanitizer violations

**Checkpoint**: CI-ready build, 100% test pass, clean sanitizer

---

## Dependencies & Execution Order

### Phase Dependencies

```text
Phase 1 (Setup):            No dependencies
    ↓
Phase 2 (Foundational):     Depends on Phase 1
    ↓
Phase 3 (Event Subsystem):  Depends on Phase 2
    ↓
Phase 4 (Scheduler):        Depends on Phase 2
    ↓
Phase 5 (Data Repository):  Depends on Phase 2
    ↓
Phase 6 (System FSM):       Depends on Phase 3, Phase 5
    ↓
Phase 7 (Event Loop):       Depends on Phase 3, Phase 4, Phase 5, Phase 6
    ↓
Phase 8 (Linux Backend):    Depends on Phase 2
    ↓
Phase 9 (Simulator App):    Depends on Phase 7, Phase 8
    ↓
Phase 10 (Polish):          Depends on all phases
```

### Within Each Phase

- Tests for the module are written alongside implementation (not TDD-first,
  but as co-located verification)
- Headers before implementation
- Core module before its test

### Parallel Opportunities

| Phase | Tasks that can run in parallel |
|---|---|
| Phase 1 | T002 (warnings.cmake) can overlap with T001 |
| Phase 2 | T005, T006, T007 (3 platform ports) fully parallel |
| Phase 3 | T008+T009 (headers) parallel; T010+T011 (source) parallel after headers |
| Phase 4 | Single implementation file — sequential |
| Phase 5 | Single implementation file — sequential |
| Phase 6 | T019 (mode_guard header) parallel with T020 (impl); T022 (FSM header) parallel with T023 (impl) |
| Phase 7 | Single implementation file — sequential |
| Phase 8 | T028, T029, T030 (3 Linux backend files) fully parallel |
| Phase 9 | T031 (main.c) sequential; T032 (test) after T031 |
| Phase 10 | T033, T034 (CMake) parallel; T035, T036, T037 sequential |

---

## Implementation Strategy

### MVP First

1. Complete Phase 1–2: Setup + Foundational types
2. Complete Phase 3–4–5: Event queue + Scheduler + Repository (P1 modules)
3. Complete Phase 6–7: FSM + Event loop (P1 orchestration)
4. Complete Phase 8–9: Linux backend + Simulator (P1 validation)
5. **STOP and VALIDATE**: All tests pass, simulator runs scenario

### Incremental Delivery

| Increment | Phases | Delivers |
|---|---|---|
| Increment 1 | 1, 2 | Buildable project structure + types |
| Increment 2 | 3, 4, 5 | Event subsystem + scheduler + repository — unit-testable in isolation |
| Increment 3 | 6, 7 | FSM + event loop — full dispatch cycle works |
| Increment 4 | 8, 9 | Linux integration — end-to-end simulation |
| Increment 5 | 10 | CI-ready with sanitizers |

---

## Summary

| Category | Count |
|---|---|
| Total tasks | 37 |
| Parallel-marked tasks [P] | 10 |
| Header files | 11 |
| Source files | 11 |
| Linux backend files | 3 |
| App entry point | 1 |
| Test files | 7 |
| CMake files | 3 |
