# Feature Specification: 9B — Temperature Calibration

**Feature Branch**: `phase9b-temperature`

**Created**: 2026-07-15

## User Stories

### US1 — Pure Temperature Conversion (P1)

**What**: MAX raw TOF → Q16 join → ratiometric resistance → RTD interpolation → per-device correction → TemperatureCandidate.

**Test**: Q16 join, zero/missing reference, sentinel/short/open, resistance ratio boundary, RTD interpolation golden vectors.

### US2 — Stateful CalibrationService (P1)

**What**: `CalibrationService` nhận raw, gọi pure converter, publish `TemperatureResult`, post `EVT_TEMPERATURE_RESULT_READY`.

**Test**: Duplicate/stale/out-of-order reject, invalid không update filter, reset/profile change reset history.

### US3 — Full-stack Temperature Scenario (P1)

**What**: MAX peer → SPI → driver → raw mailbox → calibration → repository → snapshot.

**Test**: Valid nominal, boundary, invalid reference, timeout, late completion.

## Requirements

- **FR-001**: Q16 join từ MAX integer/fraction raw.
- **FR-002**: Reference/path gain-offset correction.
- **FR-003**: Strictly monotonic RTD table + linear interpolation.
- **FR-004**: Per-device temperature gain/offset.
- **FR-005**: `EVT_TEMPERATURE_RESULT_READY` posted sau mỗi accepted input.
- **FR-006**: Provenance = MEASURED cho live production, ESTIMATED cho simulated.

## Success Criteria

- Temperature unit, service và full-stack scenarios pass.
- No heap/I/O/platform trong processing.
- Phase 8 regressions pass.
