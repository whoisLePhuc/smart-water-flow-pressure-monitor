# ADR-002: Pattern Budget

**Status**: ACCEPTED
**Date**: 2026-07-16
**Version**: 1.0.0

## Context

As the firmware is refactored, there is a risk of introducing unnecessary design patterns that increase complexity without proven value. The codebase must maintain embedded-appropriate simplicity — patterns should serve a specific purpose, not be applied for their own sake.

A pattern budget classifies every design pattern into four categories: always allowed (CORE), allowed with bounded scope (LOCAL), allowed only with review (CONDITIONAL), and forbidden (AVOID).

## Decision

Adopt a pattern budget with four categories. Every pattern used in the codebase is classified. New patterns not yet in the budget require an ADR amendment with problem statement and test evidence.

## Category Definitions

| Category | Meaning | Enforcement |
|----------|---------|-------------|
| **CORE** | Always allowed — fundamental to architecture | No gate |
| **LOCAL** | Allowed within bounded scope — scope explicitly documented | Code review |
| **CONDITIONAL** | Allowed ONLY with problem statement, test proving value, and explicit review | Gate: problem statement required |
| **AVOID** | Forbidden — use CORE or LOCAL alternative | Architecture check |

## Pattern Catalog

### CORE Patterns (always allowed)

| Pattern | Rationale | Example in Codebase |
|---------|-----------|-------------------|
| Double Buffer (atomic swap) | Thread-safe snapshot without locks | `DataRepository` — two `RuntimeSnapshot` buffers, C11 atomic index |
| Event Queue (priority + delivery) | Deterministic event processing | `AppEventQueue` — 5 priority levels, FIFO within priority |
| Finite State Machine (explicit guard) | Provable mode transitions | `SystemModeManager` — 6 modes, 53 transitions with guard context |
| Typed Write Function | Type-safe mutation without offsetof | `txn_write_flow()`, `txn_write_pressure()` — compile-time safe |
| Explicit Composition | No global defaults, clear dependency graph | `AppComposition` in Phase 7 |
| Port/Adapter (narrow interface) | Platform abstraction without HAL pollution | `monotonic_clock_port.h`, `AdcPort` |

### LOCAL Patterns (bounded scope)

| Pattern | Scope Boundary | Rationale | Max Instances |
|---------|---------------|-----------|---------------|
| Compatibility Wrapper | Migration phase only (max Phase 11) | Enables incremental migration | 6 (one per accept_*) |
| Facade | One per feature boundary | Hides internal service complexity | 4 (measurement, storage, connectivity, power) |
| Handler Registration | Event dispatch only | Replaces central switch | 1 (`EventMediator`) |
| Builder (state machine) | Protocol encoding only | Validated DTO construction | 2 (telemetry, storage) |
| Repository Transaction | DataRepository only | Explicit begin/commit/abort | 1 (`RepoWriteTxn`) |

### CONDITIONAL Patterns (needs review)

| Pattern | Precondition | Problem Statement Required |
|---------|-------------|---------------------------|
| Observer (fan-out subscriber) | Domain requires multiple independent reactions to same event | Yes — show why single handler is insufficient |
| Decorator (trace/fault injection) | Test-only — NEVER in production build | Yes — show what fault scenario isn't covered by fake port |
| Pool / Object Cache | Profiling shows allocation in hot path | Yes — show allocation profile and pool versus inline comparison |

### AVOID Patterns (forbidden)

| Pattern | Reason | Alternative |
|---------|--------|-------------|
| God Header | Single header defining ALL types | Domain-owned type headers |
| Global Singleton / Default Accessor | Hidden dependency, untestable | `AppComposition` + explicit injection |
| Dynamic Allocation (malloc) | Non-deterministic, OOM risk on STM32 | Static allocation only |
| Virtual Functions / Vtables | Runtime overhead on STM32 | Compile-time polymorphism (function pointers) |
| Dependency Injection Container | Overkill for fixed-allocation embedded | Static composition in `AppComposition` |
| Plugin System / Dynamic Loading | Out of scope | Static linking |
| God Facade / God Service | Knows every internal detail | One facade per feature boundary |
| Runtime Type Information (RTTI) | Not available in C | C11 `_Generic` or explicit type enums |
| Global `#ifdef` Platform in Logic | Code pollution | Port/Adapter pattern |

## Consequences

**Positive**:
- Prevents over-engineering — every pattern must justify itself
- Clear guidance for new contributors
- Pattern budget is version-controlled and reviewable

**Negative**:
- Requires initial effort to classify all existing patterns
- May slow down prototyping (but that's intentional — prototype != production)
- Budget must be maintained as new patterns are introduced

## Amendments

| Date | Change | Author |
|------|--------|--------|
| 2026-07-16 | Initial version v1.0.0 | Sisyphus |
