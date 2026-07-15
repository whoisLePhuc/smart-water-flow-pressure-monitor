# Feature Specification: Sim Phase 4 — Stateful Device Peers

**Feature Branch**: `sim-phase4-peers`

**Created**: 2026-07-15

## User Stories

### US1 — MAX35103 Peer (P1)

**What**: Stateful emulator peer: config/event-timing/cycle/INT/result register. Fault injection.

**Test**: Normal cycle, missing INT, duplicate INT, SPI failure, reset, late completion.

### US2 — ZSSC3241 Peer (P1)

**What**: Stateful peer: sleep/convert/ready, one-shot, EOC, configurable latency.

**Test**: Normal EOC, polling, stuck busy, fatal status, bus recovery.

### US3 — F-RAM Peer (P2)

**What**: Minimal peer để chứng minh shared-I2C arbitration. Read/write/ack.

**Test**: ZSSC + F-RAM contention qua I2cBusManager.

## Requirements

- **FR-001**: Peer không tự post EVT_*_RAW_READY — chỉ tạo device/GPIO evidence.
- **FR-002**: Peer output có DATA_ORIGIN_SIMULATED_DEVICE.
- **FR-003**: Peer không tạo MeasurementBindingReference.
- **FR-004**: Fault plan injectable qua action queue.

## Success Criteria

- Driver ↔ provider ↔ peer integration tests chứng minh canonical event sequence.
- Tất cả fault plan scenarios pass.
