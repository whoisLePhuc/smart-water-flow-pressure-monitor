# Implementation Plan: Sim Phase 5 — Simulation Harness & Scenario Runner

**Branch**: `sim-phase5-harness` | **Date**: 2026-07-15

## Technical Context

**Depends on**: Phase 1–4 (full stack)
**New files**: Harness, scenario parser, trace sink

## Acceptance Criteria

- [ ] Harness create/destroy lifecycle
- [ ] Manifest v1 parser: strict validation, action schedule, fault injection
- [ ] Normalized trace: time, event, state, generation, outcome
- [ ] End-to-end scenario chạy deterministic
- [ ] 2 lần chạy cùng manifest → cùng trace
