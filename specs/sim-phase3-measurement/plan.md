# Implementation Plan: Sim Phase 3 — Portable Measurement Vertical Slice

**Branch**: `sim-phase3-measurement` | **Date**: 2026-07-15

## Summary

Triển khai portable driver/service cho MAX35103 và ZSSC3241, I2cBusManager, và processing stubs. Đây là portable code chạy trên cả Linux và STM32.

## Technical Context

**Depends on**: Phase 0 (core runtime), Phase 2 (platform providers)
**New modules**: MAX driver, ZSSC driver, I2cBusManager, processing stubs

## Project Structure

```
2.firmware/src/
├── drivers/
│   ├── max35103.c/h         (portable, via SPI port)
│   └── zssc3241.c/h         (portable, via I2cBusManager)
├── infrastructure/
│   └── i2c_bus_manager.c/h  (shared I2C owner)
└── services/
    ├── measurement_manager.c/h
    └── processing_stubs.c/h
```

## Acceptance Criteria

- [ ] MAX: IRQ → SPI → raw_ready sequence pass
- [ ] ZSSC: sample_due → I2C → EOC → raw_ready sequence pass
- [ ] I2cBusManager: arbitration, timeout, recovery
- [ ] Stub: simulated origin + estimated provenance
