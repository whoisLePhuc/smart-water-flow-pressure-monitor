# Binary Size Baseline: Phase 0

**Created**: 2026-07-16
**Commit**: `780c12b5c3be7362f7d2fbed2741fb290ab46c9d`

## Release Build

**Config**: `-DCMAKE_BUILD_TYPE=Release -DENABLE_SANITIZERS=OFF`
**Target**: `linux_sim`

| Section | Size (bytes) | Notes |
|---------|-------------|-------|
| .text | 13,059 | Code — all firmware logic + services + tests |
| .data | 712 | Initialized data |
| .bss | 14,256 | Uninitialized data (mostly AppEventQueue 4,128 + DataRepository 2,288) |
| **Total** | **28,027** | |

## Debug Build

**Config**: `-DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON`
**Target**: `linux_sim`

| Section | Size (bytes) | Notes |
|---------|-------------|-------|
| .text | 1,222,731 | Debug code with sanitizer instrumentation |
| .data | 39,120 | Sanitizer metadata |
| .bss | 9,797,524 | Sanitizer shadow memory |
| **Total** | **11,059,375** | Not representative — sanitizer overhead |

## Notes

- Debug binary size is dominated by AddressSanitizer + UBSan instrumentation (~10MB overhead). Release size is representative.
- STM32 target measurements are **deferred** — ARM GCC toolchain not currently available.
- Key tracking metrics for refactoring: `.text` (code size), `.bss` (static allocation).
- Expected increase in Phase 10 (battery): +500-1000 bytes `.text`, minimal `.bss` increase (PowerSnapshot ~16 bytes).
