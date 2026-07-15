---
description: "Tasks for Phase 9E — pipeline integration and stub retirement"
---

# Tasks: 9E — Pipeline Integration & Stub Retirement

- [ ] T9E01 Wire temperature real path into MeasurementManager/event router
- [ ] T9E02 Wire flow real path (pair with temperature, same source token)
- [ ] T9E03 Wire pressure real path into event pipeline
- [ ] T9E04 Update simulator golden scenarios to real algorithm expectations
- [ ] T9E05 Remove processing_stubs.c/h from CMakeLists.txt and production build
- [ ] T9E06 Verify: `rg -n "processing_stub" 2.firmware/src` = 0
- [ ] T9E07 Verify: full-stack scenarios pass with real algorithms
- [ ] T9E08 Verify: determinism (5× replay, same trace)
- [ ] T9E09 Update README with processing implementation status
