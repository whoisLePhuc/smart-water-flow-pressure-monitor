/**
 * Event Mediator — Implementation
 *
 * Static handler table with linear scan dispatch.
 * No heap allocation. Fixed capacity (16 handlers).
 */

#include "event/event_mediator.h"
#include <string.h>

/* ── Handler table entry ── */

typedef struct {
    EventId       event_id;
    EventHandler  handler;
    void         *context;
    bool          active;
} HandlerTableEntry;

/* ── Static handler table ── */

static HandlerTableEntry s_table[EVENT_MEDIATOR_MAX_HANDLERS];
static uint8_t           s_count = 0;

/* ── API ── */

void event_mediator_init(void)
{
    memset(s_table, 0, sizeof(s_table));
    s_count = 0;
}

EventMediatorResult event_mediator_register(EventId event_id, EventHandler handler, void *context)
{
    if (!handler)
        return EVENT_MEDIATOR_INVALID_PARAM;

    /* Check for duplicate */
    for (uint8_t i = 0; i < s_count; i++) {
        if (s_table[i].active && s_table[i].event_id == event_id) {
            return EVENT_MEDIATOR_DUPLICATE;
        }
    }

    /* Check capacity */
    if (s_count >= EVENT_MEDIATOR_MAX_HANDLERS)
        return EVENT_MEDIATOR_TABLE_FULL;

    /* Register */
    s_table[s_count].event_id = event_id;
    s_table[s_count].handler  = handler;
    s_table[s_count].context  = context;
    s_table[s_count].active   = true;
    s_count++;

    return EVENT_MEDIATOR_OK;
}

EventMediatorResult event_mediator_dispatch(const AppEvent *event)
{
    if (!event)
        return EVENT_MEDIATOR_INVALID_PARAM;

    for (uint8_t i = 0; i < s_count; i++) {
        if (s_table[i].active && s_table[i].event_id == event->id) {
            s_table[i].handler(event, s_table[i].context);
            return EVENT_MEDIATOR_OK;
        }
    }

    return EVENT_MEDIATOR_UNHANDLED;
}

uint8_t event_mediator_handler_count(void)
{
    return s_count;
}
