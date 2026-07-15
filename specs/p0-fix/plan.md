# Implementation Plan: P0 — Generation, Dispatch & Data Model Fix

**Branch**: `p0-fix` | **Date**: 2026-07-15

## Summary

Sửa 4 architectural defects blocking simulator: data model completeness,
generation isolation, event dispatch rewrite, queue integrity.

## Technical Context

**Language/Version**: C11
**Testing**: CTest + address/UB sanitizer
**Files affected**: ~15 files (headers, sources, tests)

## Constitution Check

| Principle | Compliance |
|---|---|
| II — Non-Blocking Runtime | Dispatch rewrite ensures no event loss |
| III — Layered Architecture | Event router enforces layer boundaries |
| IV — Single-Writer Ownership | Generation isolation per domain |
| V — Deterministic Testability | All fixes validated by unit tests |

## Acceptance Criteria

- [ ] `data_is_production()` guard with unit tests
- [ ] Generation 0 consistent semantics across FSM and queue
- [ ] Event router delivers each event range to correct owner
- [ ] Budget dispatch: dequeue ≤ dispatch, no event loss
- [ ] FSM actions executed and cleared every turn
- [ ] Scheduler → queue → router → owner integration test passes
- [ ] Queue true reservation: critical always has slot
- [ ] All existing tests still pass
