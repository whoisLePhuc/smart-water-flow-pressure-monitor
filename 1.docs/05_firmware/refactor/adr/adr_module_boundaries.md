# ADR-001: Module Boundary Dependency Rules

**Status**: ACCEPTED
**Date**: 2026-07-16
**Version**: 1.0.0

## Context

The firmware codebase currently has a flat structure where all modules share wide include paths via `FIRMWARE_INCLUDE_DIRS`. A single God Header (`data_model.h`) is included by almost every module. This makes dependency direction invisible to both developers and CI tools.

To enable independent testing, bounded-context ownership, and platform portability, we need explicit dependency rules that can be enforced automatically.

## Decision

Adopt a six-layer architecture with strict dependency direction. Each layer may only depend on layers below it through defined public interfaces.

### Six-Layer Model

```
Application  (app)
    ↓
Services     (services)
    ↓
Domain       (domain)
    ↓
Infrastructure (infrastructure)
    ↓
Drivers      (drivers)
    ↓
Platform     (platform)
```

### Dependency Matrix

| From ↓ \ To → | domain | services | infrastructure | protocols | drivers | platform |
|--------------|--------|----------|----------------|-----------|---------|----------|
| **domain** | ✅ common types only | ❌ | ❌ | ❌ | ❌ | ❌ |
| **services** | ✅ | N/A | ✅ narrow contracts | ❌ encoding | ❌ concrete | ❌ |
| **infrastructure** | ✅ contracts only | ❌ | N/A | ❌ | ❌ | ❌ |
| **protocols** | ✅ read models | ❌ internals | ❌ | ✅ port contracts | ❌ | ❌ |
| **drivers** | ❌ | ❌ | ❌ | ❌ | N/A | ✅ port contracts |
| **app** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### Detailed Rules

| Rule ID | Source Layer | Forbidden Target | Example Violation | Enforcement |
|---------|-------------|------------------|-------------------|-------------|
| R01 | domain | infrastructure | `domain/foo.c` → `#include "infrastructure/event/data_model.h"` | ERROR |
| R02 | domain | platform | `domain/foo.c` → `#include "platform/include/…"` | ERROR |
| R03 | domain | drivers | `domain/foo.c` → `#include "drivers/max35103.h"` | ERROR |
| R04 | domain | services | `domain/foo.c` → `#include "services/…"` | ERROR |
| R05 | domain | app | `domain/foo.c` → `#include "app/…"` | ERROR |
| R06 | services | infrastructure concrete | `services/foo.c` → direct SPI/I2C call | WARNING |
| R07 | services | drivers | `services/foo.c` → `#include "drivers/max35103.h"` | WARNING |
| R08 | services | platform | `services/foo.c` → `#include "platform/include/…"` | ERROR |
| R09 | drivers | domain | `drivers/foo.c` → `#include "domain/…"` | ERROR |
| R10 | drivers | services | `drivers/foo.c` → `#include "services/…"` | ERROR |
| R11 | infrastructure | platform | `infrastructure/foo.c` → `#include "platform/include/…"` | ERROR |
| R12 | protocols | services | `protocols/foo.c` → `#include "services/…"` | ERROR |

### Allowed Dependencies

| From | To | Condition |
|------|----|-----------|
| domain | domain/common | Shared metadata types |
| services | domain | Narrow ports for functional calls |
| services | infrastructure | Event queue, repository, time |
| infrastructure | domain | Contract types for event payloads, snapshot |
| protocols | domain | Read models for encoding |
| drivers | platform | HAL abstractions for I/O |
| app | all | Composition root — only entry point |

## Exception Process

1. Any violation of these rules requires an ADR amendment
2. Exception must include: rule ID, reason, affected files, mitigation plan, review date
3. Exceptions are temporary — must have explicit removal phase
4. Architecture check allowlist tracks all active exceptions

## Consequences

**Positive**:
- Clear ownership — each type has one defined layer
- CI-enforceable — no manual review needed for dependency direction
- Platform independence — core can be tested without STM32 HAL
- Independent testability — each layer can be tested with fakes

**Negative**:
- Initial compliance effort — some existing code violates rules
- Requires compatibility layer during migration
- May feel restrictive for quick prototypes

## Amendments

| Date | Change | Author |
|------|--------|--------|
| 2026-07-16 | Initial version v1.0.0 | Sisyphus |
