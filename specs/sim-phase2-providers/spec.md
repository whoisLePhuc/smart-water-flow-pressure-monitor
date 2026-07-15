# Feature Specification: Sim Phase 2 — Linux Platform Providers

**Feature Branch**: `sim-phase2-providers`

**Created**: 2026-07-15

**Status**: Draft

**Input**: AI_SIMULATOR_IMPLEMENTATION_PLAN.md Phase 2

## User Scenarios

### US1 — SPI Provider (P1)

**What**: Async SPI transaction provider với peer forwarding, terminal completion, no-completion fault.

**Test**: Submit SPI request → peer nhận → terminal completion. No-completion → timeout → không có completion fabricate.

### US2 — Physical I2C Provider (P1)

**What**: Physical I2C bus provider dưới I2cBusManager, peer registry theo address.

**Test**: ZSSC + F-RAM submit qua bus manager → một provider serialize. Duplicate address reject.

### US3 — GPIO Provider (P1)

**What**: Linux GPIO line model với level/edge, INT/EOC assertion, arm generation.

**Test**: Schedule INT assert → GPIO evidence đúng thời điểm. Missing edge → timeout.

## Requirements

- **FR-001**: SPI provider accept correlated async request, validate resource/buffer/capacity.
- **FR-002**: Completion mang PlatformOperationIdentity + correlation + generation.
- **FR-003**: No-completion fault không tự fabricate success/failure.
- **FR-004**: I2C provider peer registry theo bus + 7-bit address.
- **FR-005**: Bus recovery increment resource generation, invalidate old completions.
- **FR-006**: GPIO line evidence scheduled qua action queue với action class riêng.
- **FR-007**: Provider contract test suite reusable cho Linux backend và STM32.

## Success Criteria

- SPI/I2C/GPIO providers pass platform contract suite với fake peer.
- Admission, completion exactly once, timeout/no-completion, reset, stale generation tests pass.
- Cùng contract suite chạy được trên Linux backend.
