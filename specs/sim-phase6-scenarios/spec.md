# Feature Specification: Sim Phase 6 — Initial Scenario Catalog

**Feature Branch**: `sim-phase6-scenarios`

**Created**: 2026-07-15

## User Stories

### US1 — Core/Runtime Scenarios (P1)

Budget không mất event, same-timestamp ordering, stale generation, duplicate completion, reset, queue pressure.

### US2 — MAX Scenarios (P1)

Normal cycle, INT already asserted, missing INT/timeout, duplicate INT, SPI failure, reset.

### US3 — ZSSC Scenarios (P1)

Normal EOC, polling, due-busy, missing EOC, fatal status, I2C failure, bus recovery, F-RAM contention.

### US4 — Metadata Scenarios (P2)

Simulated origin preserved, binding mismatch reject, service/calibration isolation, one snapshot per turn.

## Requirements

- **FR-001**: Scenarios dùng canonical event IDs từ data_model.h.
- **FR-002**: Mỗi scenario có normalized trace golden.
- **FR-003**: Scenario không inject EVT_*_RAW_READY — đi qua driver path.
- **FR-004**: Service/calibration sample không update production side effects.

## Success Criteria

- ~30 scenarios pass.
- Trace deterministic qua nhiều lần chạy.
- Fault matrix từ doc 92 section 12 phủ hết.
