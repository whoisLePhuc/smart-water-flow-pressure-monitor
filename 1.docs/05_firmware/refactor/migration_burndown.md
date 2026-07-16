# Migration Burn-down Report — Phase 9 Baseline

**Created**: 2026-07-16

## Legacy Artifacts Remaining

| Artifact | Count | Target | Removal Phase |
|----------|-------|--------|---------------|
| `accept_*` call sites (production) | 5 | 0 | P9 (current) |
| `data_model.h` includes | 13 | 0 | P11 |
| Compatibility CMake targets | 3 | 0 | P9 |
| Global defaults | 5 | 0 | P11 |
| Architecture violations | 0 (errors) / 6 (warnings) | 0 | P11 |

## Notes

- `infrastructure` and `services` compatibility targets kept for build stability
- `core` INTERFACE target kept as utility
- 5 global defaults documented with deprecation markers
- All architecture violations are allowlisted with resolution phases

## CTest

**46 tests total** — 44 pass, 2 pre-existing failures (test_volume_arithmetic, test_volume_duplicate)
