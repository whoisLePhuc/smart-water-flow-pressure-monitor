# Feature Specification: Sim Phase 1 — Deterministic Linux Backend Foundation

**Feature Branch**: `sim-phase1-foundation`

**Created**: 2026-07-15

**Status**: Draft

**Input**: AI_SIMULATOR_IMPLEMENTATION_PLAN.md Phase 1

## User Scenarios & Testing

### US1 — Virtual Clock (Priority: P1)

**What**: Monotonic virtual clock với AdvanceTo/AdvanceBy, wall-time separation, reset policy.

**Why P1**: Tất cả deterministic timing phụ thuộc vào clock.

**Independent Test**: Advance 100us → Now() = 100. AdvanceTo trước now bị reject. Wall clock step không ảnh hưởng monotonic.

### US2 — Scheduled Action Queue (Priority: P1)

**What**: Bounded priority queue với total-order key, cancel/invalidate bằng generation.

**Why P1**: Platform providers cần queue để schedule completion.

**Independent Test**: Insert 3 actions với due_us khác nhau → dequeue đúng thứ tự. Cancel bằng generation → action không fire.

### US3 — RunController (Priority: P1)

**What**: RunOneTurn + RunUntilIdle với bounded budget, livelock detection.

**Why P1**: Kết nối virtual clock + action queue + firmware event loop.

**Independent Test**: Turn không có work → IDLE. Turn có work → PROGRESS. max_turns cạn → STEP_LIMIT.

## Requirements

- **FR-001**: Virtual clock ở chế độ deterministic, time chỉ advance khi được gọi.
- **FR-002**: `AdvanceTo(t)` không cho t < now.
- **FR-003**: Wall clock và monotonic time là hai domain độc lập.
- **FR-004**: Scheduled-action queue có total-order key: due_us → action_class → resource_id → generation → sequence.
- **FR-005**: Queue có bounded capacity, overflow visible.
- **FR-006**: Cancel bằng `(action_id, expected_generation)`.
- **FR-007**: `RunOneTurn` thực hiện: dispatch due actions → scheduler due → event loop → pending actions → snapshot.
- **FR-008**: `RunUntilIdle` có max_turns, max_actions, max_time, max_same_time_repeats.
- **FR-009**: Queue không dùng pointer có lifetime ngắn.
- **FR-010**: Scheduler vẫn sở hữu job table; action queue không thay scheduler.

## Success Criteria

- Same input/seed tạo normalized trace giống nhau qua nhiều lần chạy.
- Clock không đi lùi ở mọi test case.
- Action queue total order đúng với mọi permutation insertion.
- `RunUntilIdle` phát hiện IDLE, STEP_LIMIT, LIVELOCK.
- All unit tests pass + no sleep() trong deterministic mode.
