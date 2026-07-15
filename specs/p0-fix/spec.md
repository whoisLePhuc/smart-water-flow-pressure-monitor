# Feature Specification: P0 — Generation, Dispatch & Data Model Fix

**Feature Branch**: `p0-fix`

**Created**: 2026-07-15

**Status**: Draft

## Context

Four architectural defects block the simulator build:

1. **Generation handling**: Event envelope bị ghi đè sau post, FSM so generation sai domain.
2. **Event dispatch**: Batch-dequeue gây mất event, không có router, FSM actions không execute.
3. **Data model**: Thiếu `MeasurementBindingReference`, `data_is_production()` guard, tên enum chưa canonical.
4. **Queue**: Reserved capacity là quota, thiếu fairness.

## User Stories

### US1 — Data Model Completeness (P0)

**What**: Hoàn thiện data model: binding reference, production guard, payload types, canonical enum names.

**Independent Test**: `data_is_production()` trả đúng cho mọi tổ hợp. `MeasurementBindingReference` được copy qua repository.

### US2 — Generation Isolation (P0)

**What**: Generation không bị ghi đè, FSM chỉ check system domain, gen 0 semantics nhất quán.

**Independent Test**: Event từ scheduler domain đi qua FSM không bị stale. Gen 0 không bị reject.

### US3 — Event Dispatch Rewrite (P0)

**What**: Dispatch từng event, event router, execute FSM actions, guard provider thật, scheduler integration.

**Independent Test**: Mỗi event range đến đúng owner. Budget không mất event. FSM action được execute.

### US4 — Queue Integrity (P1)

**What**: True reservation, fairness bound, SourceEventToken đúng.

**Independent Test**: Critical event luôn có slot. Low priority không starvation.
