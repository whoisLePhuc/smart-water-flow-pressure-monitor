---
description: "Task list for P0 fixes — data model, generation, dispatch, queue"
---

# Tasks: P0 — Generation, Dispatch & Data Model Fix

## Phase A: Data Model Completeness [US1]

- [ ] TA01 [P] [US1] Add `MeasurementBindingReference` struct to `2.firmware/include/core/data_model.h`
- [ ] TA02 [P] [US1] Add `binding` field to `ResultMetadata` in `2.firmware/include/core/data_model.h`
- [ ] TA03 [P] [US1] Rename `MEASUREMENT_PURPOSE_*` to `MEAS_PURPOSE_*` in `2.firmware/include/core/data_model.h`
- [ ] TA04 [US1] Add `data_is_production()` function to `2.firmware/include/core/data_repository.h` and implement in `2.firmware/src/core/data_repository.c`
- [ ] TA05 [P] [US1] Add MAX/ZSSC/I2C payload types to `2.firmware/include/core/data_model.h`
- [ ] TA06 [US1] Write unit tests for `data_is_production()` in `2.firmware/tests/test_data_repository.c`

## Phase B: Generation Isolation [US2]

- [ ] TB01 [US2] Remove `source_generation` overwrite in `2.firmware/src/core/app_event_loop.c` line 69
- [ ] TB02 [US2] Add `event_is_system_event()` helper to `2.firmware/src/core/system_fsm.c`
- [ ] TB03 [US2] FSM stale check only applies to system events in `2.firmware/src/core/system_fsm.c`
- [ ] TB04 [US2] Document and enforce `generation==0` == not-set semantics in `2.firmware/src/core/system_fsm.c`
- [ ] TB05 [US2] Write unit tests for generation domains (system vs scheduler, gen 0 passthrough) in `2.firmware/tests/test_system_fsm.c`

## Phase C: Event Dispatch Rewrite [US3]

- [ ] TC01 [US3] Create `2.firmware/include/core/app_event_router.h` — `EventOwner` enum, `route_event()` API
- [ ] TC02 [US3] Create `2.firmware/src/core/app_event_router.c` — route by event ID range
- [ ] TC03 [US3] Rewrite `app_event_loop_run_once()` — dispatch loop: dequeue-one → route → dispatch → next
- [ ] TC04 [US3] Add FSM action execution turn in `app_event_loop_run_once()`: read `pending_actions`, execute, `clear_actions`
- [ ] TC05 [US3] Replace hard-coded guards with `mode_guard_capture()` call in `app_event_loop_run_once()`
- [ ] TC06 [US3] Add scheduler integration: call `scheduler_dispatch_due()` at start of each turn, post due events to queue
- [ ] TC07 [US3] Add `2.firmware/tests/test_app_event_router.c` — route all event ranges to correct owner
- [ ] TC08 [US3] Add integration test: scheduler → queue → router → owner flow in `2.firmware/tests/test_linux_simulation.c`

## Phase D: Queue Integrity [US4]

- [ ] TD01 [US4] Fix reserved capacity in `2.firmware/src/core/app_event_queue.c`: background events capped at `capacity - reserved_critical`
- [ ] TD02 [US4] Add fairness bound: per-priority min quota in `2.firmware/src/core/app_event_queue.c`
- [ ] TD03 [US4] Add starvation detection counter in `2.firmware/src/core/app_event_queue.c`
- [ ] TD04 [US4] Verify `SourceEventToken` publication-per-turn semantics in `2.firmware/src/core/data_repository.c`
- [ ] TD05 [US4] Write tests for true reservation, fairness, starvation in `2.firmware/tests/test_event_queue.c`
