# Phase 11 AI Implementation Plan — Leak Detection

> Based on: `17_leak_detection.md` (canonical firmware contract), `05_leak_detection_algorithm_baseline.md`, `06_leak_detection_state_and_evidence_model.md`
> 
> Prerequisite: Phase 10 — all 30 tests pass, VolumeAccumulator + StorageService operational.

## Workstreams

### 11A — Data Model Migration
- Update `LeakState`: remove `UNKNOWN`, keep `NORMAL/SUSPECTED/CONFIRMED`
- Update `LeakEvaluationStatus`: remove `NOT_EVALUATED/INSUFFICIENT_DATA/EVALUATING/COMPLETED`, add `NOT_READY/ACTIVE/DEGRADED/UNAVAILABLE`
- Add `LeakEvidenceFlag`, `LeakEvidencePhase`, `LeakPrimaryReason`, `LeakSeverity` enums
- Update `LeakDetectionResult` with new fields per §8.8

### 11B — Configuration
- Create `LeakDetectionConfig` with all threshold/duration parameters (§11.1)
- Implement config validator (§11.2 rules)

### 11C — Evidence Trackers
- Generic evidence tracker FSM (INACTIVE→PENDING→ACTIVE→CLEAR_PENDING→SUSPENDED)
- Continuous-flow tracker
- High-flow/burst tracker
- Pressure diagnostic tracker

### 11D — Leak Detection Service
- Evidence aggregation + state machine (NORMAL/SUSPECTED/CONFIRMED)
- Admission gate + input snapshot
- Result publication + event generation
- Boot/reset/clear semantics

### 11E — Tests
- Unit: tracker FSM, config validation, state transitions
- Integration: service with FlowResult/PressureResult
- E2E: boot → accumulate → restore

## Rules
- No production thresholds — use TEST_ONLY synthetic values
- No F-RAM persistence changes
- Phase 8-10 regressions must pass
