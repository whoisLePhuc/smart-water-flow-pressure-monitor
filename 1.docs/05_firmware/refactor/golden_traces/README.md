# Golden Trace Manifest: Phase 0 Baseline

**Created**: 2026-07-16
**Baseline Commit**: `780c12b5c3be7362f7d2fbed2741fb290ab46c9d`
**Build**: Release (`-DCMAKE_BUILD_TYPE=Release`)

## Trace Files

| Scenario | Source Test | File | Size | MD5 | Runs Verified |
|----------|-------------|------|------|-----|---------------|
| core | `test_scenarios_core` | `core.trace` (205B) | 205 bytes | `d7498d3d59d661781acb70159f0122a3` | 5/5 identical |
| max | `test_scenarios_max` | `max.trace` (182B) | 182 bytes | `afa64bf1a3e1b0fbccc37a6a220fc09c` | 5/5 identical |
| zssc | `test_scenarios_zssc` | `zssc.trace` (182B) | 182 bytes | `f1f6b30cb96e5c23b54670378de3c1ce` | 5/5 identical |

## Determinism Verification

All 3 traces produced byte-identical output across 5 consecutive runs.
This confirms the simulation system is deterministic at baseline.

## Comparison Method

To verify after refactoring:
```bash
# Run scenario, compare with golden trace
./2.firmware/build-release/tests/test_scenarios_core > /tmp/current.trace
diff 1.docs/05_firmware/refactor/golden_traces/core.trace /tmp/current.trace
# No diff = behavior unchanged
```
