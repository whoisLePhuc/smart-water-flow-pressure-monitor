# Implementation Plan: 9B — Temperature Calibration

**Branch**: `phase9b-temperature` | **Date**: 2026-07-15

## Summary

Replace temperature processing stub with real calibration: RTD-based conversion with ratiometric measurement, table interpolation, per-device correction.

## Technical Context

**Depends on**: 9A numeric utilities
**New module**: `CalibrationService` in `src/services/`
**Reference**: `13_temperature_calibration.md`

## Acceptance Criteria

- [ ] Q16 join from MAX raw TOF
- [ ] Reference/path correction applied
- [ ] RTD table interpolation with monotonic validation
- [ ] Per-device gain/offset
- [ ] Duplicate/stale/out-of-order rejected
- [ ] Full-stack: MAX peer → TemperatureResult → repository
- [ ] All Phase 8 + 9A tests pass
