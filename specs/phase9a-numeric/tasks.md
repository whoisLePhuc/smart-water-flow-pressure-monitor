---
description: "Tasks for Phase 9A — contract freeze and numeric foundation"
---

# Tasks: 9A — Contract Freeze & Numeric Foundation

- [ ] T9A01 Audit `ResultMetadata`, `MeasurementPurpose`, `DataOrigin`, `DataProvenance`, `MeasurementBindingReference` alignment with FW-CORE docs
- [ ] T9A02 Create `2.firmware/src/infrastructure/numeric/checked_math.h/.c` — checked add/sub/mul with overflow, rounding (nearest + tie), saturate
- [ ] T9A03 Create `2.firmware/src/infrastructure/numeric/interpolation.h/.c` — monotonic table validation, linear interpolation
- [ ] T9A04 Create `2.firmware/src/infrastructure/numeric/gain_offset.h/.c` — fixed-point gain/offset, range classification
- [ ] T9A05 Create `2.firmware/include/services/sensor_profile.h` — profile structs for temp/flow/pressure, validators
- [ ] T9A06 Create test profiles (immutable, versioned) in `2.firmware/tests/fixtures/`
- [ ] T9A07 Write unit tests for checked_math, interpolation, gain_offset
- [ ] T9A08 Write profile validation tests
- [ ] T9A09 Update CMakeLists.txt for new numeric library + test targets
- [ ] T9A10 Verify: all Phase 8 tests still pass
