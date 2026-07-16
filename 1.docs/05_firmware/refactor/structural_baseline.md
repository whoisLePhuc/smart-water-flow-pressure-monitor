# Structural Baseline: Phase 0

**Created**: 2026-07-16
**Build**: Debug (with sanitizers)
**Commit**: `780c12b5c3be7362f7d2fbed2741fb290ab46c9d`

## Data Structure Sizes

| Type Name | sizeof (bytes) | Notes |
|-----------|---------------|-------|
| `RuntimeSnapshot` | 744 | Largest aggregate — contains 5 result structs + 2 contexts |
| `AppEvent` | 64 | 48-byte fields + 16-byte inline payload |
| `DataRepository` | 2,288 | 2 × RuntimeSnapshot buffers + metadata + inactive buffer |
| `AppEventQueue` | 4,128 | 64 × AppEvent ring buffer (4096) + head/tail/counters |
| `VolumeState` | 128 | 19 fields (volumes, remainders, sequences, anchors) |
| `LeakDetectionResult` | 144 | 22 fields (state, evidence flags, timestamps, versions) |
| `SystemModeManager` | 120 | FSM internal state + transitions |
| `ModeGuardContext` | 28 | 14 boolean flags + 4 generation counters |
| `SystemModeContext` | 40 | 7 fields (mode, generations, timestamps) |
| `ResultMetadata` | 104 | 18 fields (metadata for every measurement result) |

## Notes

- `AppEventQueue` is the dominant memory consumer at 4,128 bytes due to the 64-slot `AppEvent` ring buffer.
- `DataRepository` at 2,288 bytes reflects the double-buffer design (744 × 2 ≈ 1,488 + inactive copy + metadata).
- `RuntimeSnapshot` (744 bytes) is the key structure to watch during refactoring. Adding new fields (like `PowerSnapshot` in Phase 10) will increase this size.
- These measurements include struct padding and are from the Debug build. Sizes in Release may differ slightly due to alignment optimizations.
