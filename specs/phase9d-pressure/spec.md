# Feature Specification: 9D — Pressure Measurement Processing

**Feature Branch**: `phase9d-pressure`

**Created**: 2026-07-15

## User Stories

### US1 — Pure Pressure Mapping (P1)

**What**: ZSSC raw U24/status → endpoint mapping → field trim → range classification → PressureCandidate.

**Test**: U24 min/mid/max, byte order, invalid status, endpoint equal/reversed, signed pressure.

### US2 — PressureProcessingService (P1)

**What**: Nhận ZSSC raw, gọi pure pipeline, publish `PressureResult`, post `EVT_PRESSURE_RESULT_READY`.

**Test**: Duplicate/stale/reject, bus recovery, filter init/update/reset.

### US3 — ZSSC Full-stack Scenario (P1)

**What**: ZSSC peer → I2C → driver → raw → pressure → repository.

**Test**: Valid, missing EOC, I2C failure, bus recovery, valid flow when pressure unavailable.

## Requirements

- **FR-001**: U24 parsing không dựa vào host endianness.
- **FR-002**: Không double-apply factory correction.
- **FR-003**: Overpressure survival ≠ valid measurement range.
- **FR-004**: Invalid status không tạo valid 0 Pa.

## Success Criteria

- Pressure numeric/metadata/service/full-stack tests pass.
- Factory correction không bị double-apply.
- Flow không bị block khi pressure unavailable.
