---
description: "Tasks for Phase 9B — temperature calibration"
---

# Tasks: 9B — Temperature Calibration

- [ ] T9B01 Implement temperature pure pipeline: Q16 join, ratio, RTD interpolation, correction
- [ ] T9B02 Implement `CalibrationService` stateful: accept raw, call pipeline, publish result
- [ ] T9B03 Add `EVT_TEMPERATURE_RESULT_READY` event wiring to router
- [ ] T9B04 Write Q16 join unit tests (boundary, overflow)
- [ ] T9B05 Write RTD interpolation golden vector tests
- [ ] T9B06 Write CalibrationService duplicate/stale/reset contract tests
- [ ] T9B07 Write MAX full-stack temperature scenario
- [ ] T9B08 Update CMakeLists.txt for new modules
