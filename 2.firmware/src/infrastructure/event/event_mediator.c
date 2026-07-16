#include "event/event_mediator.h"
#include <string.h>

void event_mediator_init(EventMediator *m)
{
    if (m) { memset(m, 0, sizeof(*m)); }
}

EventMediatorResult event_mediator_register(EventMediator *m, EventId id, EventHandler handler, void *ctx)
{
    if (!m || !handler) return EVENT_MEDIATOR_INVALID_PARAM;
    for (uint8_t i = 0; i < m->count; i++) {
        if (m->entries[i].active && m->entries[i].event_id == id)
            return EVENT_MEDIATOR_DUPLICATE;
    }
    if (m->count >= EVENT_MEDIATOR_MAX_HANDLERS) return EVENT_MEDIATOR_TABLE_FULL;
    m->entries[m->count].event_id = id;
    m->entries[m->count].handler  = handler;
    m->entries[m->count].context  = ctx;
    m->entries[m->count].active   = true;
    m->count++;
    return EVENT_MEDIATOR_OK;
}

EventMediatorResult event_mediator_dispatch(EventMediator *m, const AppEvent *event)
{
    if (!m || !event) return EVENT_MEDIATOR_INVALID_PARAM;
    for (uint8_t i = 0; i < m->count; i++) {
        if (m->entries[i].active && m->entries[i].event_id == event->id) {
            m->entries[i].handler(event, m->entries[i].context);
            return EVENT_MEDIATOR_OK;
        }
    }
    return EVENT_MEDIATOR_UNHANDLED;
}

uint8_t event_mediator_handler_count(const EventMediator *m)
{
    return m ? m->count : 0;
}
