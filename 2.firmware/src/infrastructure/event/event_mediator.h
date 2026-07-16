#ifndef SWFPM_EVENT_MEDIATOR_H
#define SWFPM_EVENT_MEDIATOR_H

/* =================================================================
 * Event Mediator — Handler Registration and Dispatch
 *
 * Replaces the central byte-range router (app_event_router.c)
 * with a handler registration table. Each domain registers its
 * own event handler. New domains (e.g. power) can be added
 * without modifying infrastructure code.
 *
 * Capacity: EVENT_MEDIATOR_MAX_HANDLERS (default 16)
 * Policy: single handler per event (no fan-out in Phase 4)
 * ================================================================= */

#include <stdint.h>
#include <stdbool.h>
#include "infrastructure/event/event_id.h"
#include "event/app_event_queue.h"  // AppEvent

/* ── Result codes ── */

typedef enum {
    EVENT_MEDIATOR_OK,
    EVENT_MEDIATOR_TABLE_FULL,
    EVENT_MEDIATOR_DUPLICATE,
    EVENT_MEDIATOR_UNHANDLED,
    EVENT_MEDIATOR_INVALID_PARAM
} EventMediatorResult;

/* ── Handler signature ── */

typedef void (*EventHandler)(const AppEvent *event, void *context);

/* ── Config ── */

#define EVENT_MEDIATOR_MAX_HANDLERS 32

/* ── API ── */

/* Initialize the mediator — clears handler table */
void event_mediator_init(void);

/* Register a handler for a specific EventId.
 * Returns: OK, TABLE_FULL (capacity exhausted), DUPLICATE (handler already registered) */
EventMediatorResult event_mediator_register(EventId event_id, EventHandler handler, void *context);

/* Dispatch an event to its registered handler.
 * Returns: OK (handled), UNHANDLED (no handler registered), INVALID_PARAM (null event) */
EventMediatorResult event_mediator_dispatch(const AppEvent *event);

/* Get the number of currently registered handlers */
uint8_t event_mediator_handler_count(void);

#endif /* SWFPM_EVENT_MEDIATOR_H */
