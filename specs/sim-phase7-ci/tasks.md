---
description: "Task list for Sim Phase 7 — CI, determinism, equivalence"
---

# Tasks: Sim Phase 7 — CI, Determinism & Equivalence Gate

- [ ] TG01 Configure warnings-as-errors hardening: update `2.firmware/cmake/warnings.cmake`
- [ ] TG02 Configure ASan/UBSan cho CI: update `.github/workflows/ci.yml` hoặc tạo mới
- [ ] TG03 Write determinism test: `2.firmware/tests/system/test_determinism.c` — chạy mỗi scenario 5 lần, so trace
- [ ] TG04 Write seeded property tests: `2.firmware/tests/system/test_properties.c`
- [ ] TG05 Verify contract test suite runs on Linux: `2.firmware/tests/contract/`
- [ ] TG06 Update root CMakeLists.txt cho CI targets
