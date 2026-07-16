# Compatibility Policy — Wrapper Lifecycle

**Created**: 2026-07-16

## Purpose

During refactoring, old APIs are kept as compatibility wrappers so that existing consumers can migrate incrementally. This policy defines how wrappers are created, tracked, and removed — ensuring they have bounded lifetime.

## Wrapper Lifecycle

### Phase 1: Creation

When an API is replaced:
1. Create a compatibility wrapper with the OLD name that delegates to the NEW API
2. Add `@deprecated` comment with removal phase
3. Create a removal task in the Phase 9 or Phase 11 plan
4. Track usage count via `grep`

### Phase 2: Migration

During migration:
- New code MUST use the new API directly
- Old code MAY use the compatibility wrapper
- Usage count should decrease monotonically

### Phase 3: Removal

In the designated removal phase:
- Delete the compatibility wrapper
- If any usage remains, migrate it first
- Architecture check confirms 0 usages

## Naming Convention

| Type | Format | Example |
|------|--------|---------|
| File (header) | `*_legacy.h` | `data_model_legacy.h` |
| File (source) | `*_compat.c` | `telemetry_builder_compat.c` |
| Function | `old_name` (same name, new impl) | `data_repository_accept_flow(...)` → delegates to `txn_*` |

## Deprecation Marker Format

```c
/**
 * @deprecated Removed in Phase N.
 * Use <new_api_description> instead.
 * Migration task: <task_id>
 */
```

## Wrapper Catalog

| Old API | New API | Created | Removed | Task | Usage (Phase 0) |
|---------|---------|---------|---------|------|-----------------|
| `data_repository_accept_*()` | `txn_begin/write/commit` | P5 | P9 | P9.2 | 4 |
| `route_event()` + `dispatch_to_owner()` | `event_mediator_dispatch()` | P4 | P9 | P9.2 | 0 |
| `data_model.h` (re-export) | Domain headers | P3 | P11 | P11.1 | 0 .c files |

## Tracking

- Usage count is tracked in the [Dashboard](../phase_execution_plan.md#18-tracking-dashboard-đề-xuất)
- Each PR should reduce the count
- Architecture check warns when new code uses compatibility APIs
- Removal is verified by `grep` in the removal phase

## Policy Rules

1. Every wrapper MUST have a deprecation marker with removal phase
2. Every wrapper MUST have a corresponding removal task in Phase 9 or 11
3. Wrapper MUST NOT contain business logic (pure delegation only)
4. Wrapper usage MUST be tracked via `grep` count
5. No wrapper may survive beyond Phase 11
