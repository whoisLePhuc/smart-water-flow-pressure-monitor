# Implementation Plan: Sim Phase 7 — CI, Determinism & Equivalence Gate

**Branch**: `sim-phase7-ci` | **Date**: 2026-07-15

## Technical Context

**Depends on**: All prior phases
**Focus**: CI configuration, determinism verification, contract test portability

## Acceptance Criteria

- [ ] Warnings-as-errors enforced
- [ ] ASan/UBSan passes
- [ ] Deterministic replay N×5 identical
- [ ] Seeded property tests for ordering/generation/capacity
- [ ] Contract test suite runs on Linux
