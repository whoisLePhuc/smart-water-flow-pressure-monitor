# Ownership & Lifetime Convention

**Created**: 2026-07-16
**Applies to**: All new APIs from Phase 3 onwards

## Purpose

Define clear ownership and lifetime rules for all resource types. Every function signature in new code must make it obvious who owns the data and how long it's valid.

## General Rules

1. **One owner**: Every mutable object has exactly one owner — the single writer.
2. **Explicit lifetime**: Every pointer parameter has documented lifetime.
3. **No hidden sharing**: No module holds a pointer to another module's internal state across a dispatch cycle.

## Resource Types

### Pointer Return Values

| Pattern | Ownership | Lifetime |
|---------|-----------|----------|
| `const T* function()` | Caller reads, does NOT own | Documented window — caller must not hold across dispatch cycle |
| `T* function() && caller frees` | Caller owns, MUST free/release | Until caller releases |
| `const RuntimeSnapshot*` from snapshot_acquire | Caller reads, does NOT own | Until `snapshot_release()` — max one dispatch cycle |

### Buffer Parameters

| Pattern | Ownership | Lifetime |
|---------|-----------|----------|
| `function(T *out, size_t size)` | Caller owns buffer, callee fills | Caller controls |
| `function(const T *in, size_t size)` | Caller owns, callee reads only | Caller controls |
| `function(T *inout, size_t size)` | Caller owns, callee modifies | Caller controls |

### Event Payloads

| Type | Lifetime |
|------|----------|
| Inline payload (`AppEvent.payload[16]`) | Copied into queue — caller can free immediately after `event_queue_post()` |
| Mailbox payload | Callee owns until consumed |
| Reference payload | Caller MUST keep alive until handler returns — NEVER across dispatch cycles |

### Snapshot Handles

| Operation | Lifetime Rule |
|-----------|---------------|
| `snapshot_acquire()` → read → `snapshot_release()` | Must complete within one dispatch cycle |
| `txn_read_snapshot(&out)` | Caller-owned copy — no lifetime issue |
| Holding handle across `publish_if_requested()` | Handle remains valid (capture-once), but data may be stale |

### Configuration Data

| Type | Ownership |
|------|-----------|
| `PowerConfig`, `AppEventQueueConfig`, etc. | Read-only after init |
| Runtime config changes | Via explicit re-init or versioned apply |

## Forbidden Patterns

| ❌ Pattern | Why | Alternative |
|-----------|-----|-------------|
| Returning pointer to internal static buffer | Race condition, hidden state | Copy to caller-provided buffer |
| Global variable accessed from multiple modules | No ownership | Explicit injection via composition |
| Module holding pointer to another module's snapshot | Stale data, use-after-free | `txn_read_snapshot()` — caller-owned copy |
| `const T*` + "don't free" without documented lifetime | Ambiguous ownership | Document window or return copy |
