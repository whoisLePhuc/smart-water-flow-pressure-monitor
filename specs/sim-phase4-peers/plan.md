# Implementation Plan: Sim Phase 4 — Stateful Device Peers

**Branch**: `sim-phase4-peers` | **Date**: 2026-07-15

## Technical Context

**Depends on**: Phase 2 (providers), Phase 3 (portable drivers)
**New files**: peer modules trong `2.firmware/src/platform/linux/peers/`

## Acceptance Criteria

- [ ] MAX peer: reset, event-timing, cycle, INT, register read, fault plans
- [ ] ZSSC peer: sleep/convert/ready, one-shot, EOC, polling, fault plans
- [ ] F-RAM peer minimal: read/write/ack
- [ ] Shared-I2C contention test pass
- [ ] Peer không post raw-ready event
