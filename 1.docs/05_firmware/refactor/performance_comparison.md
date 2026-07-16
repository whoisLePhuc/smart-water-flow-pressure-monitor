# Performance Comparison: Baseline vs Post-Refactor

**Created**: 2026-07-16

## Structural Sizes

| Type | Phase 0 (baseline) | Phase 11 (current) | Delta | Budget |
|------|-------------------|--------------------|-------|--------|
| RuntimeSnapshot | 744 B | 744 B | 0 B | ±10% |
| AppEvent | 64 B | 64 B | 0 B | — |
| DataRepository | 2,288 B | 2,288 B | 0 B | — |
| AppEventQueue | 4,128 B | 4,128 B | 0 B | — |
| VolumeState | 128 B | 128 B | 0 B | — |
| LeakDetectionResult | 144 B | 144 B | 0 B | — |
| SystemModeManager | 120 B | 120 B | 0 B | — |

## Binary Size (Release)

| Section | Phase 0 | Phase 11 | Delta | Budget |
|---------|---------|----------|-------|--------|
| .text | 13,059 B | 13,489 B | +430 (+3.3%) | ±5% ✅ |
| .data | 712 B | 712 B | 0 B | — |
| .bss | 14,256 B | 14,776 B | +520 (+3.6%) | ±5% ✅ |
| **Total** | **28,027 B** | **28,977 B** | **+950 (+3.4%)** | **±5% ✅** |

## Analysis

- RuntimeSnapshot size unchanged (PowerSnapshot was added via union/struct field, fits within existing padding)
- Binary size increased by 3.4% — well within the 5% budget
- Increase attributed to: event mediator (handler table), repo transaction API, port contracts, facades, power service code, protocol DTOs
- No increase in .data section (no new global variables)
- Minor .bss increase from static handler tables and adapter state

## Conclusion

All performance metrics are within budget. No regression.
