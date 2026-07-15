# Feature Specification: Sim Phase 3 — Portable Measurement Vertical Slice

**Feature Branch**: `sim-phase3-measurement`

**Created**: 2026-07-15

**Status**: Draft

## User Stories

### US1 — MAX35103 Portable Driver (P1)

**What**: MAX portable driver: IRQ evidence → SPI ops → coherent raw mailbox → EVT_MAX_RAW_READY.

**Test**: GPIO INT → EVT_MAX_IRQ_ASSERTED → SPI ops → EVT_MAX_SPI_COMPLETED → driver validates → EVT_MAX_RAW_READY.

### US2 — ZSSC3241 Portable Driver + I2cBusManager (P1)

**What**: ZSSC driver qua I2cBusManager: one-shot command → EOC → result read → EVT_PRESSURE_RAW_READY.

**Test**: EVT_PRESSURE_SAMPLE_DUE → I2C one-shot → EOC → I2C read → EVT_PRESSURE_RAW_READY.

### US3 — Processing Stubs (P2)

**What**: Deterministic processing stub cho flow/temperature/pressure, output provenance=ESTIMATED.

**Test**: Raw in → stub result out với purpose/origin/provenance đúng.

## Requirements

- **FR-001**: MAX driver không tự post EVT_MAX_RAW_READY từ IRQ — chỉ sau SPI completion.
- **FR-002**: I2cBusManager là single owner của physical I2C port.
- **FR-003**: ZSSC driver không overlap conversion — một attempt tại một thời điểm.
- **FR-004**: Processing stub output có DATA_ORIGIN_SIMULATED_DEVICE.
- **FR-005**: Stub provenance là PROVENANCE_ESTIMATED với reason flag.

## Success Criteria

- Portable driver/service state tests pass bằng fake ports.
- Không cần stateful device peer để unit test portable logic.
