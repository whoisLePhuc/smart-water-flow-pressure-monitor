# Header Convention — Public vs Private Includes

**Created**: 2026-07-16

## Scope

This convention applies to all new code written from Phase 3 onwards. Legacy code (existing at baseline) is exempt until migrated.

## Rules

### Public Headers

Public headers are headers that form the API boundary of a module. They define types and functions that consumers outside the module may use.

| Property | Rule |
|----------|------|
| **Location** | `include/<module>/` (e.g., `include/domain/common/metadata.h`) |
| **Include guard** | `#ifndef SWFPM_<MODULE>_<FILE>_H` / `#define ...` / `#endif` |
| **Dependencies** | Only include public headers of dependency modules. NEVER include private headers of other modules. |
| **Visibility** | Listed in `target_include_directories(<target> PUBLIC ...)` |
| **Stability** | Public API is stable within a major version. Breaking changes require ADR + compatibility wrapper. |

### Private Headers

Private headers are internal to a module. They are NOT exposed to consumers.

| Property | Rule |
|----------|------|
| **Location** | `src/<module>/` — same directory as implementation files |
| **Visibility** | Listed in `target_include_directories(<target> PRIVATE ...)` |
| **Include pattern** | Only included from within the same module |
| **No restriction** | May change freely — no compatibility wrapper needed |

### Include Path Format

```
// Public header — from another module
#include "domain/measurement/measurement_types.h"

// Private header — from same module  
#include "internal_helpers.h"

// Platform port — via ports layer
#include "ports/adc_port.h"

// Infrastructure
#include "infrastructure/event/event_id.h"
```

### Target Include Directory Convention

Each CMake target declares its include directories as follows:

```cmake
# Domain target — INTERFACE (header-only)
target_include_directories(fw_domain_measurement
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/measurement
)

# Infrastructure target — STATIC
target_include_directories(fw_infra_event
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/event
)
```

## Enforcement

- Architecture check script verifies forbidden include patterns
- CMake target_include_directories limits visibility — no global `FIRMWARE_INCLUDE_DIRS`
- Pull request review checks include hygiene
