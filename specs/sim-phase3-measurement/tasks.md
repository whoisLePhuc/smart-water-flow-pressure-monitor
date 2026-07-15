---
description: "Task list for Sim Phase 3 — portable measurement slice"
---

# Tasks: Sim Phase 3 — Portable Measurement Vertical Slice

- [ ] TC01 Create I2cBusManager: `2.firmware/include/infrastructure/i2c_bus_manager.h` + `2.firmware/src/infrastructure/i2c_bus_manager.c`
- [ ] TC02 Create MAX35103 portable driver: `2.firmware/include/drivers/max35103.h` + `2.firmware/src/drivers/max35103.c`
- [ ] TC03 Create ZSSC3241 portable driver: `2.firmware/include/drivers/zssc3241.h` + `2.firmware/src/drivers/zssc3241.c`
- [ ] TC04 Create MeasurementManager: `2.firmware/include/services/measurement_manager.h` + `2.firmware/src/services/measurement_manager.c`
- [ ] TC05 Create processing stubs: `2.firmware/include/services/processing_stubs.h` + `2.firmware/src/services/processing_stubs.c`
- [ ] TC06 Write MAX unit tests: `2.firmware/tests/test_max35103.c`
- [ ] TC07 Write ZSSC unit tests: `2.firmware/tests/test_zssc3241.c`
- [ ] TC08 Write I2cBusManager tests: `2.firmware/tests/test_i2c_bus_manager.c`
- [ ] TC09 Update CMakeLists.txt with new modules and tests
