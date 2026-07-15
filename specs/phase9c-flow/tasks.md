---
description: "Tasks for Phase 9C — flow measurement processing"
---

# Tasks: 9C — Flow Measurement Processing

- [ ] T9C01 Implement flow pure pipeline: TOF join, differential, velocity, flow
- [ ] T9C02 Implement temperature pairing logic (identity, age, freshness)
- [ ] T9C03 Implement `FlowComputationService`: accept raw, pair temp, call pipeline, publish result
- [ ] T9C04 Add `EVT_FLOW_RESULT_READY` event wiring
- [ ] T9C05 Write TOF differential/sign/zero/reverse unit tests
- [ ] T9C06 Write temperature pairing contract tests
- [ ] T9C07 Write flow golden vector tests
- [ ] T9C08 Write full-stack temperature + flow scenario (one snapshot)
- [ ] T9C09 Update CMakeLists.txt
