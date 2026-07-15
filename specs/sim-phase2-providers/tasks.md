---
description: "Task list for Sim Phase 2 — Linux platform providers"
---

# Tasks: Sim Phase 2 — Linux Platform Providers

- [ ] TB01 Create `2.firmware/include/platform/providers/linux_spi_provider.h`
- [ ] TB02 Create `2.firmware/src/platform/linux/providers/linux_spi_provider.c` — async SPI, peer forward, terminal completion
- [ ] TB03 Create `2.firmware/include/platform/providers/linux_i2c_provider.h`
- [ ] TB04 Create `2.firmware/src/platform/linux/providers/linux_i2c_provider.c` — physical I2C, address registry, bus recovery
- [ ] TB05 Create `2.firmware/include/platform/providers/linux_gpio_provider.h`
- [ ] TB06 Create `2.firmware/src/platform/linux/providers/linux_gpio_provider.c` — line state, INT/EOC, arm gen
- [ ] TB07 Create `2.firmware/tests/test_linux_providers.c` — contract test suite
- [ ] TB08 Update CMakeLists.txt cho providers
