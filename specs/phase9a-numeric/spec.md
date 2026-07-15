# Feature Specification: 9A — Contract Freeze & Numeric Foundation

**Feature Branch**: `phase9a-numeric-foundation`

**Created**: 2026-07-15

## User Stories

### US1 — Metadata & Binding Contract (P1)

**What**: Đóng băng `ResultMetadata`, `MeasurementBindingReference`, version/generation field widths, zero/invalid semantics.

**Test**: Metadata round-trip giữ nguyên purpose/origin/provenance/binding. data_is_production() với đủ tổ hợp.

### US2 — Checked Numeric Utilities (P1)

**What**: Pure functions cho checked add/sub/mul, rounding, interpolation, gain/offset, range classification.

**Test**: Boundary vectors: zero, min, max, negative, tie, one-below/above overflow.

### US3 — Profile & Fixture Foundation (P1)

**What**: Immutable test profiles cho temperature/flow/pressure, validators reject malformed.

**Test**: profile_id, schema version, monotonic table, range bounds.

## Requirements

- **FR-001**: `ResultMetadata` phản ánh đúng canonical fields từ FW-CORE-004 v0.2.
- **FR-002**: Checked arithmetic helpers có overflow detection và rounding policy rõ ràng.
- **FR-003**: Linear interpolation với monotonic table check.
- **FR-004**: Profile validator reject non-monotonic table, invalid range, bad integrity.
- **FR-005**: Test profiles có `profile_id`, schema version, qualification status.

## Success Criteria

- Shared type/layout tests pass.
- Numeric helper unit tests pass under sanitizer.
- Profile validators reject malformed artifacts.
- Existing Phase 8 scenarios still pass.
