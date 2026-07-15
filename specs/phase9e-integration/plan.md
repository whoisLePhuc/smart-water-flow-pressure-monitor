# Implementation Plan: 9E — Pipeline Integration & Stub Retirement

**Branch**: `phase9e-integration` | **Date**: 2026-07-15

## Summary

Wire real temperature/flow/pressure services, update simulator goldens, retire stubs.

## Technical Context

**Depends on**: 9B, 9C, 9D (all real processing paths)
**Verification**: `rg -n "processing_stub"`, full CTest, determinism replay

## Acceptance Criteria

- [ ] Temperature real path wired → simulated scenario passes
- [ ] Flow real path wired → temp+flow one-snapshot passes
- [ ] Pressure real path wired → full-stack passes
- [ ] Golden scenarios updated to real algorithm results
- [ ] Stubs removed from production build
- [ ] 24 DoD items pass
- [ ] README updated
