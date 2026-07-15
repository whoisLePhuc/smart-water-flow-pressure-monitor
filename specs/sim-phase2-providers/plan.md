# Implementation Plan: Sim Phase 2 — Linux Platform Providers

**Branch**: `sim-phase2-providers` | **Date**: 2026-07-15

## Summary

Triển khai SPI, I2C, GPIO platform providers cho Linux backend theo contract `50_platform_abstraction.md`. Mỗi provider là struct với function pointers, completion qua scheduled-action queue.

## Technical Context

**Depends on**: Phase 1 (virtual clock, action queue, run controller)
**Files**: `2.firmware/src/platform/linux/providers/` (mới)
**Testing**: Platform contract test suite (shared với STM32)

## Project Structure

```
2.firmware/
├── include/platform/providers/
│   ├── linux_spi_provider.h
│   ├── linux_i2c_provider.h
│   └── linux_gpio_provider.h
├── src/platform/linux/providers/
│   ├── linux_spi_provider.c
│   ├── linux_i2c_provider.c
│   └── linux_gpio_provider.c
└── tests/
    └── test_linux_providers.c
```

## Acceptance Criteria

- [ ] SPI submit → completion exactly once
- [ ] I2C peer registry theo address, duplicate reject
- [ ] GPIO INT/EOC assertion deterministic
- [ ] Provider contract tests pass
