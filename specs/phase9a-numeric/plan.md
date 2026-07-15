# Implementation Plan: 9A — Contract Freeze & Numeric Foundation

**Branch**: `phase9a-numeric-foundation` | **Date**: 2026-07-15

## Summary

Freeze shared measurement types, implement checked arithmetic/interpolation utilities, and create versioned test profiles before algorithm implementation.

## Technical Context

**Language/Version**: C11, fixed-width integers, no float in processing core
**New targets**: `firmware_numeric` static library
**Dependencies**: core data model types

## Constitution Check

| Principle | Compliance |
|---|---|
| IV — Single-Writer Ownership | ResultMetadata ownership unchanged |
| V — Deterministic Testability | Numeric helpers have boundary vectors |

## Acceptance Criteria

- [ ] Checked add/sub/mul với overflow detection
- [ ] Round-to-nearest + tie policy
- [ ] Linear interpolation + monotonic validation
- [ ] Gain/offset + range classification
- [ ] Profile validators
- [ ] All Phase 8 tests still pass
