# Implementation Plan: 9C — Flow Measurement Processing

**Branch**: `phase9c-flow` | **Date**: 2026-07-15

## Summary

Replace flow processing stub with real computation: TOF differential, temperature pairing, velocity/flow model, calibration.

## Technical Context

**Depends on**: 9A numeric, 9B temperature (temperature result contract)
**New module**: `FlowComputationService`
**Reference**: `14_flow_measurement_processing.md`

## Acceptance Criteria

- [ ] TOF differential + sign + zero/reverse
- [ ] Temperature pairing by identity/sample time/max age
- [ ] Velocity → volumetric flow with geometry
- [ ] Calibration application
- [ ] Missing/stale temperature → no production acceptance
- [ ] Same source event → temperature + flow → one snapshot
- [ ] No volume accumulation
