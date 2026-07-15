# Implementation Plan: Sim Phase 1 — Deterministic Linux Backend Foundation

**Branch**: `sim-phase1-foundation` | **Date**: 2026-07-15

## Summary

Xây dựng deterministic runtime foundation cho firmware simulator: virtual clock, scheduled-action queue, và run controller. Đây là lớp nền cho mọi platform provider và device peer ở các phase sau.

## Technical Context

**Language/Version**: C11, cùng toolchain với 2.firmware
**Files**: `2.firmware/src/platform/linux/` (mới) + `2.firmware/include/platform/` (mới)
**Testing**: CTest + deterministic unit tests (no sleep)

## Constitution Check

| Principle | Compliance |
|---|---|
| I — Documentation-First, Simulation-First | Tài liệu spec/plan có trước code |
| II — Cooperative Non-Blocking Runtime | RunController không block, không sleep |
| V — Deterministic Testability | Virtual clock là core của deterministic test |

## Project Structure

```
2.firmware/
├── include/platform/
│   ├── linux_virtual_clock.h      # NEW
│   ├── linux_scheduled_action_queue.h  # NEW
│   └── linux_run_controller.h     # NEW
├── src/platform/linux/
│   ├── linux_virtual_clock.c      # NEW (replace virtual_clock.c)
│   ├── linux_scheduled_action_queue.c  # NEW
│   └── linux_run_controller.c     # NEW
└── tests/
    ├── test_linux_virtual_clock.c       # NEW
    ├── test_linux_action_queue.c        # NEW
    └── test_linux_run_controller.c      # NEW
```

## Acceptance Criteria

- [ ] Virtual clock: Now(), AdvanceBy(delta), AdvanceTo(t), wall clock separation, reset
- [ ] Action queue: bounded, total order, cancel by generation, same-time livelock detect
- [ ] RunController: RunOneTurn schedule → ingest → dispatch → publish → next deadline
- [ ] RunUntilIdle: max_turns, max_actions, max_time, livelock detection
- [ ] 3 test suites pass, 100% deterministic, no sleep()
