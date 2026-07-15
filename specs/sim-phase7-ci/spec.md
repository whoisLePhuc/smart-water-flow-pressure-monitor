# Feature Specification: Sim Phase 7 — CI, Determinism & Equivalence Gate

**Feature Branch**: `sim-phase7-ci`

**Created**: 2026-07-15

## User Stories

### US1 — CI Pipeline (P1)

**What**: All test levels (unit, contract, integration, system) chạy trong CI với warnings-as-errors, ASan/UBSan.

### US2 — Determinism Gate (P1)

**What**: Mỗi scenario chạy N lần, normalized trace phải giống nhau.

### US3 — Reusable Contract Suite (P2)

**What**: Contract tests chạy trên Linux backend; có thể chạy lại trên STM32 adapter/HIL.

## Requirements

- **FR-001**: Warnings-as-errors cho toàn bộ 2.firmware.
- **FR-002**: ASan/UBSan enabled cho Debug build (trừ cross-compile).
- **FR-003**: Deterministic replay: N lần → normalized trace giống nhau.
- **FR-004**: Seeded property tests cho ordering/generation/capacity.
- **FR-005**: Realtime mode không dùng làm golden CI oracle.

## Success Criteria

- CI green với all tests.
- Deterministic replay pass 5 lần liên tiếp.
- Contract test suite chạy trên Linux.
