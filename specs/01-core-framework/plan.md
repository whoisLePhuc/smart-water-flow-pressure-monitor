# Implementation Plan: Phase 1 — Core Framework

**Branch**: `01-core-framework` | **Date**: 2026-07-14 | **Spec**: N/A (infrastructure)

**Input**: Firmware architecture docs (FW-CORE-001–005), reviewed Phase 1 structure

---

## Summary

Build the foundational cooperative event-loop core for the Smart Water Flow and
Pressure Monitor firmware. This includes the event model, monotonic scheduler,
system FSM, mode guard provider, and double-buffer data repository — all
running deterministically on Linux simulation with C11 atomics.

---

## Technical Context

**Language/Version**: C11 (C17 features where applicable), compiled with GCC
( Linux) / ARM GCC (STM32)

**Primary Dependencies**: None for portable core; `stdatomic.h` for snapshot
atomic swap; CMake 3.20+ build system

**Storage**: N/A (Phase 1 — persistent storage via `StorageService` comes in
later phase; `DataRepository` is RAM-only double-buffer)

**Testing**: Native C test runner (CTest + custom test harness); address
sanitizer, undefined behavior sanitizer in CI

**Target Platform**: Linux x86_64 (development + simulation), STM32L433RCT6
(production — Phase 1 only builds on Linux)

**Project Type**: Embedded firmware core library (static library + test
executables + Linux simulator app)

**Performance Goals**: Bounded event-loop turn budget (exact value
`NEEDS_VERIFICATION` per FW-RT-OQ-002); no dynamic allocation in production
path; no blocking calls (FW-RT-REQ-003)

**Constraints**:
- No POSIX, STM32 HAL, or RTOS dependency in portable core (FW-ARCH-REQ-002)
- No blocking I/O, busy-wait, or `sleep()` in core modules (FW-RT-REQ-003)
- Single-writer ownership for every mutable object (FW-DATA-REQ-001)
- Double-buffer snapshot with atomic active-index swap (FW-DATA-REQ-015)
- C11 `stdatomic.h` for snapshot atomicity — no platform memory-barrier
  abstraction (architecture decision per review)

**Scale/Scope**: 8 core modules, ~2500 LOC total; 1 Linux simulator app;
~15 unit/integration test files

---

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Compliance | Notes |
|---|---|---|
| I. Documentation-First, Simulation-First | ✅ | All code is Linux-first; constitution and firmware docs precede code |
| II. Cooperative Non-Blocking Runtime | ✅ | Core design: event loop, async completion, bounded steps, no blocking calls |
| III. Strict Layered Architecture | ✅ | 6-layer dependency enforced; platform ports are narrow; core includes no HAL/POSIX |
| IV. Single-Writer Ownership | ✅ | Ownership matrix defined; DataRepository double-buffer; consumer const-only |
| V. Deterministic Testability | ✅ | Virtual clock; all tests via CTest + sanitizer; deterministic event ordering |

No violations. Phase 1 implements principles II, III, IV, and V directly.

---

## Project Structure

### Documentation (this feature)

```text
specs/01-core-framework/
├── plan.md              # This file
├── quickstart.md        # Validation guide for Phase 1
└── tasks.md             # Created by /speckit.tasks
```

### Source Code

```text
3.firmware/
├── CMakeLists.txt
├── cmake/
│   └── warnings.cmake
├── include/
│   ├── core/
│   │   ├── app_event.h
│   │   ├── app_event_queue.h
│   │   ├── app_event_loop.h
│   │   ├── scheduler.h
│   │   ├── system_fsm.h
│   │   ├── mode_guard.h
│   │   ├── data_model.h
│   │   └── data_repository.h
│   └── platform/
│       ├── monotonic_clock_port.h
│       ├── system_control_port.h
│       └── platform_runtime.h
├── src/
│   ├── core/
│   │   ├── app_event.c
│   │   ├── app_event_queue.c
│   │   ├── app_event_loop.c
│   │   ├── monotonic_scheduler.c
│   │   ├── system_fsm.c
│   │   ├── mode_guard.c
│   │   └── data_repository.c
│   └── platform/
│       └── linux/
│           ├── virtual_clock.c
│           ├── linux_system_control.c
│           └── linux_platform_runtime.c
├── apps/
│   └── linux_sim/
│       └── main.c
└── tests/
    ├── test_event_queue.c
    ├── test_event_loop.c
    ├── test_scheduler.c
    ├── test_system_fsm.c
    ├── test_mode_guard.c
    ├── test_data_repository.c
    └── test_linux_simulation.c
```

**Structure Decision**: Embedded firmware core library with separate platform
backend. Portable core (`include/core/` + `src/core/`) has zero platform
dependency. Linux backend (`src/platform/linux/`) implements narrow port
interfaces. Linux simulator app (`apps/linux_sim/`) links both for end-to-end
validation. Tests are standalone executables linked against core + fake/mock
ports.

---

## Architecture & Implementation Order

### Dependency Graph

```text
platform_port (interfaces only)
    ↑
Core modules (each depends on platform ports for clock/control)
    ↑
AppEventLoop (dispatcher — depends on all core modules)
    ↑
apps/linux_sim/main.c + tests/
```

### Build Order (bottom-up)

| Step | Module | Files | Depends On |
|---|---|---|---|
| 1 | Data model types | `include/core/data_model.h` | Nothing |
| 2 | Platform ports | `include/platform/*.h` | Nothing |
| 3 | Event queue | `app_event_queue.{c,h}` | data_model.h |
| 4 | Event helpers | `app_event.{c,h}` | app_event_queue.h |
| 5 | Monotonic scheduler | `monotonic_scheduler.{c,h}` | platform ports (clock) |
| 6 | Data repository | `data_repository.{c,h}` | data_model.h, stdatomic.h |
| 7 | Mode guard | `mode_guard.{c,h}` | data_model.h |
| 8 | System FSM | `system_fsm.{c,h}` | mode_guard.h, app_event.h |
| 9 | Event loop | `app_event_loop.{c,h}` | all modules above |
| 10 | Linux platform backend | `src/platform/linux/*` | platform ports |
| 11 | Linux simulator app | `apps/linux_sim/main.c` | event loop + linux backend |
| 12 | Tests | `tests/*.c` | all modules + fake ports |

### Module Contracts

#### 1. Data Model (`data_model.h`)
- `ResultMetadata`: validity, freshness, acceptance, provenance, version fields
- `TemperatureResult`, `FlowResult`, `PressureResult`: measurement result types
- `VolumeState`, `LeakDetectionResult`: product state types
- `RuntimeSnapshot`: full system snapshot (mode + all results + volume + leak)
- `SystemMode`: INIT, NORMAL, LOW_POWER, SERVICE, RECOVERY, ERROR
- `SystemModeContext`: mode + generation + transition_sequence + timestamp
- `ModeGuardContext`: 15 boolean guards snapshot
- Event IDs: canonical enum (`EVT_*`) matching catalog in FW-CORE-003 §17.1

#### 2. Platform Ports (`monotonic_clock_port.h`, `system_control_port.h`, `platform_runtime.h`)
- `monotonic_now_us()` → `uint64_t`
- `system_request_reset(uint32_t reason)`
- `platform_init()`, `platform_poll()`

#### 3. Event Queue (`app_event_queue.h`)
- `AppEvent_t` envelope: id, source, priority, delivery class, sequence,
  correlation_id, source_generation, monotonic_timestamp_us, payload
- `app_event_queue_post()` / `app_event_queue_post_from_isr()`
- `app_event_queue_try_get()`
- 5 delivery classes: EDGE, COMPLETION, LEVEL, DEADLINE, MAILBOX
- Overflow policy: P0 critical → emergency flag; P1 measurement → reserved slot;
  config → backpressure; LCD → coalesce

#### 4. Monotonic Scheduler (`scheduler.h`)
- `SchedulerJob`: job_id, owner, event_id, deadline_us, anchor_us, period_us,
  generation, mode_mask, miss_policy, priority
- `scheduler_schedule_one_shot()` / `scheduler_schedule_periodic()`
- `scheduler_cancel()` / `scheduler_dispatch_due(now_us)`
- `scheduler_get_next_deadline()` → for PowerManager
- Anchored periodic: `deadline(n) = anchor + n × period`
- Miss policy: skip, signal-once, owner-specific

#### 5. Data Repository (`data_repository.h`)
- Double-buffer with C11 `atomic_uint_fast8_t active_index`
- `data_repository_accept_*(result, source_token)` → build inactive → atomic swap
- `data_repository_snapshot_acquire()` → capture-once read handle
- `snapshot_read_ptr()` / `data_repository_snapshot_release()`
- One final snapshot per accepted source event per turn (FW-DATA-REQ-016)

#### 6. Mode Guard (`mode_guard.h`)
- `ModeGuardContext` snapshot from published evidence
- `mode_guard_capture(event, current_state)` → immutable guard context
- Guard sources: `SystemModeManager`, `DataRepository`, diagnostics

#### 7. System FSM (`system_fsm.h`)
- `system_fsm_dispatch(manager, event, guards)` → `FsmDispatchResult`
- Pure function of (current_state, event, guards) → (next_state, actions)
- 53 transition table entries (TR-SYS-001..053) covering 6 modes
- Mode admission matrix: 6×11 capability table
- Transition record: previous_mode, new_mode, event_id, reason, sequence,
  monotonic_time_us, guard_snapshot_id, action_mask

#### 8. Event Loop (`app_event_loop.h`)
- Collect → dispatch bounded work → publish snapshot → check low-power
- Per-turn budget: event count, service-step count, execution-time
- Priority: critical > measurement > shared-resource > config > background

---

## Complexity Tracking

No complexity violations. The 8-module core follows the layered architecture
principle (Principle III). Each module has a single responsibility.
Platform ports are intentionally narrow (3 interfaces, 5 functions total).

---

## Acceptance Criteria

Phase 1 complete when:

- [ ] Build by CMake on Linux with `-Wall -Werror`
- [ ] No core module includes Linux, POSIX, or STM32 header
- [ ] Virtual clock supports `Now()`, `AdvanceBy(delta)`, `AdvanceTo(t)`
- [ ] Scheduler tests use virtual time — no real `sleep()`
- [ ] Event queue tests cover all 5 delivery classes, priority ordering,
      overflow backpressure, duplicate rejection, stale generation detection
- [ ] Event loop implements collect → dispatch → publish → low-power check
- [ ] All 53 TR-SYS-* entries present in transition table
- [ ] FSM tests guard true and false via explicit `ModeGuardContext`
- [ ] Mode guard tests prove evidence generation consistency
- [ ] DataRepository uses exactly 2 buffers; consumer never sees mixed snapshot
- [ ] Linux simulator runs: boot → NORMAL → LOW_POWER → WAKE → NORMAL
- [ ] Critical event (`EVT_CRITICAL_ERROR`) beats low-power request when both pending
- [ ] No address/undefined sanitizer violations in any test
