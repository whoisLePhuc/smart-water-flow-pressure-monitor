# Feature Specification: Sim Phase 5 — Simulation Harness & Scenario Runner

**Feature Branch**: `sim-phase5-harness`

**Created**: 2026-07-15

## User Stories

### US1 — Harness (P1)

**What**: Create/destroy Linux backend, register buses/GPIO/peers, install fixtures, init firmware, expose observations.

### US2 — Scenario Manifest v1 (P1)

**What**: Strict UTF-8 JSON manifest, validated schema, action schedule, fault injection, assertions.

### US3 — Normalized Trace (P1)

**What**: Deterministic trace record: time, event, state, generation, outcome. No pointer/pid/host data.

## Requirements

- **FR-001**: Harness không sở hữu product logic.
- **FR-002**: Manifest reject unknown fields trong strict mode.
- **FR-003**: Trace không chứa pointer address, pid, host wall clock.
- **FR-004**: Manifest validate references id+version.

## Success Criteria

- Manifest normal chạy end-to-end, outcome deterministic.
- Parser invalid-case tests và cleanup tests pass.
