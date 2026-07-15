# Implementation Plan: 9D — Pressure Measurement Processing

**Branch**: `phase9d-pressure` | **Date**: 2026-07-15

## Summary

Replace pressure stub with real processing: ZSSC U24-to-Pa mapping, field trim, range classification.

## Technical Context

**Depends on**: 9A numeric
**New module**: `PressureProcessingService`
**Reference**: `15_pressure_measurement_processing.md`

## Acceptance Criteria

- [ ] U24/status parsing (endian-neutral)
- [ ] Endpoint mapping to canonical Pa
- [ ] Field trim (no double factory correction)
- [ ] Physical/rated/application range classification
- [ ] Optional filter with monotonic dt
- [ ] Full-stack: ZSSC → PressureResult → repository
- [ ] Flow không bị block khi pressure unavailable
