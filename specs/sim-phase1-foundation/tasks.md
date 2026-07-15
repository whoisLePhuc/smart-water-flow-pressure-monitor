---
description: "Task list for Sim Phase 1 — Deterministic Linux backend foundation"
---

# Tasks: Sim Phase 1 — Deterministic Linux Backend Foundation

## Phase 1A: Virtual Clock [US1]

- [ ] TA01 [US1] Create `2.firmware/include/platform/linux_virtual_clock.h` — `LinuxVirtualClock` struct, `linux_clock_now_us()`, `linux_clock_advance_by()`, `linux_clock_advance_to()`, `linux_clock_set_wall()`, `linux_clock_reset()`
- [ ] TA02 [US1] Create `2.firmware/src/platform/linux/linux_virtual_clock.c` — implementation: uint64_t now_us, AdvanceTo rejects going backward, wall time tách biệt, reset tạo boot generation mới
- [ ] TA03 [US1] Create `2.firmware/tests/test_linux_virtual_clock.c` — test now/advance/advanceto, wall independence, reset, overflow boundary, reverse-reject

## Phase 1B: Scheduled Action Queue [US2]

- [ ] TA04 [P] [US2] Create `2.firmware/include/platform/linux_scheduled_action_queue.h` — `LinuxScheduledAction`, total-order key fields, `LinuxActionQueue`, `action_queue_init()`, `action_queue_schedule()`, `action_queue_cancel()`, `action_queue_dispatch_due()`, `action_queue_next_deadline()`
- [ ] TA05 [US2] Create `2.firmware/src/platform/linux/linux_scheduled_action_queue.c` — bounded min-heap, total order (due → class → resource_id → gen → seq), cancel by generation, same-time livelock detect, capacity overflow
- [ ] TA06 [US2] Create `2.firmware/tests/test_linux_action_queue.c` — insertion order, cancel, generation mismatch, full queue, same-time ordering, livelock detection

## Phase 1C: RunController [US3]

- [ ] TA07 [P] [US3] Create `2.firmware/include/platform/linux_run_controller.h` — `LinuxRunController`, `RunControllerStatus` enum (PROGRESS/IDLE/STEP_LIMIT/LIVELOCK/ERROR), `run_controller_init()`, `run_controller_run_one_turn()`, `run_controller_run_until_idle()`
- [ ] TA08 [US3] Create `2.firmware/src/platform/linux/linux_run_controller.c` — RunOneTurn: dispatch due actions → scheduler_dispatch_due → post to queue → app_event_loop_run_once → execute FSM actions → publish snapshot → next deadline. RunUntilIdle: loop with max_turns/max_actions/max_time bounds, livelock detection
- [ ] TA09 [US3] Create `2.firmware/tests/test_linux_run_controller.c` — idle detection, progress detection, step limit, livelock detection

## Phase 1D: Integration & Migration

- [ ] TA10 Update `2.firmware/src/platform/linux/CMakeLists.txt` — add new source files
- [ ] TA11 [P] Update `2.firmware/tests/CMakeLists.txt` — add new test targets
- [ ] TA12 Migrate old `virtual_clock.c/h` references to new `linux_virtual_clock`

## Dependencies

```text
TA01-TA03 (Virtual Clock) ──→ TA04-TA06 (Action Queue) ──→ TA07-TA09 (RunController)
                                                                        ↓
                                                              TA10-TA12 (Integration)
```
