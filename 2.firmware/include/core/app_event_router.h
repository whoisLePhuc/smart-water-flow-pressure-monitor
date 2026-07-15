#ifndef SWFPM_APP_EVENT_ROUTER_H
#define SWFPM_APP_EVENT_ROUTER_H

#include "core/app_event_queue.h"
#include "core/system_fsm.h"
#include "core/data_repository.h"

/* Event owner — the module responsible for handling an event */
typedef enum {
    EVENT_OWNER_UNKNOWN,
    EVENT_OWNER_SYSTEM_FSM,        /* 0x0100–0x01FF: mode/state transitions */
    EVENT_OWNER_MEASUREMENT,       /* 0x0200–0x02FF: measurement pipeline */
    EVENT_OWNER_PRODUCT,           /* 0x0300–0x03FF: volume, leak, snapshot */
    EVENT_OWNER_INFRASTRUCTURE,    /* 0x0380–0x03FF: I2C, shared bus */
    EVENT_OWNER_CONFIG_STORAGE,    /* 0x0400–0x04FF: config, storage */
    EVENT_OWNER_TIME_REPORTING,    /* 0x0500–0x05FF: RTC, reporting */
    EVENT_OWNER_BLE_CELLULAR,      /* 0x0600–0x06FF: BLE, 4G */
    EVENT_OWNER_DISPLAY_HEALTH,    /* 0x0700–0x07FF: LCD, health */
} EventOwner;

/* Route result — describes what to do with the event */
typedef struct {
    EventOwner  owner;
    bool        handled;       /* true if routed to a registered owner */
} RouteResult;

/* Look up which owner should handle an event based on its ID range */
RouteResult route_event(const AppEvent *event);

/* Dispatch an event to its owner.
 * Returns true if the event was consumed. */
bool dispatch_to_owner(
    const AppEvent *event,
    SystemModeManager *fsm,
    DataRepository *repo);

#endif /* SWFPM_APP_EVENT_ROUTER_H */
