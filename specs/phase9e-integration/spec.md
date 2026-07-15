# Feature Specification: 9E — Pipeline Integration & Stub Retirement

**Feature Branch**: `phase9e-integration`

**Created**: 2026-07-15

## User Stories

### US1 — Wire Real Processing (P1)

**What**: Wiring temperature → flow → pressure real paths into event pipeline, replace stub composition.

### US2 — Golden Scenarios (P1)

**What**: Simulator golden scenarios updated to expect real algorithm results instead of stub.

### US3 — Stub Retirement (P2)

**What**: Remove processing_stubs.c/h, ensure no unintended ESTIMATED results in production path.

## Requirements

- **FR-001**: MAX source event tạo temperature + flow → one final snapshot.
- **FR-002**: ZSSC source event tạo pressure → one final snapshot.
- **FR-003**: Simulated/replayed/service origin không tạo production side effect.
- **FR-004**: Không còn `processing_stub` trong production composition.

## Success Criteria

- All golden scenarios pass with real algorithm results.
- `rg -n "processing_stub"` trả về 0 trong production code.
- 24 DoD items pass.
