#ifndef SWFPM_EVENT_MEDIATOR_H
#define SWFPM_EVENT_MEDIATOR_H

#include <stdint.h>
#include <stdbool.h>
#include "infrastructure/event/event_id.h"
#include "infrastructure/queues/app_event_queue.h"

typedef enum {
    EVENT_MEDIATOR_OK,
    EVENT_MEDIATOR_TABLE_FULL,
    EVENT_MEDIATOR_DUPLICATE,
    EVENT_MEDIATOR_UNHANDLED,
    EVENT_MEDIATOR_INVALID_PARAM
} EventMediatorResult;

typedef void (*EventHandler)(const AppEvent *event, void *context);

#define EVENT_MEDIATOR_MAX_HANDLERS 32

typedef struct {
    EventId      event_id;
    EventHandler handler;
    void        *context;
    bool         active;
} HandlerTableEntry;

typedef struct {
    HandlerTableEntry entries[EVENT_MEDIATOR_MAX_HANDLERS];
    uint8_t           count;
} EventMediator;

void                event_mediator_init(EventMediator *m);
EventMediatorResult event_mediator_register(EventMediator *m, EventId event_id, EventHandler handler, void *context);
EventMediatorResult event_mediator_dispatch(EventMediator *m, const AppEvent *event);
uint8_t             event_mediator_handler_count(const EventMediator *m);

#endif
