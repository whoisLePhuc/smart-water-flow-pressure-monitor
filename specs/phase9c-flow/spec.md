# Feature Specification: 9C — Flow Measurement Processing

**Feature Branch**: `phase9c-flow`

**Created**: 2026-07-15

## User Stories

### US1 — Pure Flow Conversion (P1)

**What**: Upstream/downstream TOF raw → differential → pair with temperature → geometry/compensation → volumetric flow → FlowCandidate.

**Test**: TOF differential sign, zero flow, forward/reverse, temperature pairing freshness, geometry mismatch.

### US2 — FlowComputationService (P1)

**What**: Nhận MAX raw token, pair temperature, gọi pure pipeline, publish `FlowResult`, post `EVT_FLOW_RESULT_READY`.

**Test**: Duplicate/stale/out-of-order reject, incompatible temperature reject.

### US3 — Temperature + Flow Full-stack (P1)

**What**: MAX peer → raw → temperature result → flow result → repository. Cùng source event tạo TemperatureResult và FlowResult.

**Test**: One final snapshot với cả temperature và flow trong cùng turn.

## Requirements

- **FR-001**: FlowComputationService pair temperature bằng identity/sample time/max age.
- **FR-002**: Missing/stale temperature block production acceptance.
- **FR-003**: Reverse flow tạo negative flow_ul_per_s.
- **FR-004**: Temperature + flow cùng source event → một final snapshot.

## Success Criteria

- Flow algorithm pass unit/integration/golden tests.
- Temperature pairing và freshness policy đúng.
- No volume update trong Phase 9.
