#include "event/app_event_router.h"
#include "event/event_mediator.h"
#include "event/mode_guard.h"
#include "platform/include/monotonic_clock_port.h"
#include <string.h>

/* Try mediator first, then fall back to legacy switch */
static bool try_mediator_then_legacy(
    const AppEvent *event,
    EventMediator *mediator,
    SystemModeManager *fsm,
    DataRepository *repo)
{
    EventMediatorResult mr = event_mediator_dispatch(mediator, event);
    if (mr == EVENT_MEDIATOR_OK)
        return true;

    /* Fall back to legacy byte-range dispatch for unmigrated events */
    RouteResult route = route_event(event);
    if (!route.handled) return false;

    switch (route.owner) {
    case EVENT_OWNER_SYSTEM_FSM: {
        ModeGuardContext guards;
        memset(&guards, 0, sizeof(guards));
        guards.core_ready = true;
        guards.recovery_can_run = true;
        guards.wake_sources_armed = true;
        guards.service_authorized = true;
        guards.safe_service_boundary = true;
        guards.safe_to_resume_normal = true;
        guards.return_normal = true;
        guards.reinitialize_allowed = true;

        FsmDispatchResult fsm_result = system_fsm_dispatch(fsm, event, &guards);
        if (fsm_result == FSM_TRANSITION_COMMITTED) {
            SourceEventToken token;
            data_repository_init_token(&token, event->id);
            SystemModeContext ctx = system_fsm_get_context(fsm);
            data_repository_accept_mode(repo, &ctx, &token);
        }
        return true;
    }
    default:
        return false;
    }
}

/* Legacy router — kept for backward compatibility during migration */

RouteResult route_event(const AppEvent *event)
{
    RouteResult result = { EVENT_OWNER_UNKNOWN, false };
    if (!event) return result;

    uint16_t range = (uint16_t)(event->id & 0xFF00);

    switch (range) {
    case 0x0100:
        result.owner = EVENT_OWNER_SYSTEM_FSM;
        result.handled = true;
        break;
    case 0x0200:
        result.owner = EVENT_OWNER_MEASUREMENT;
        result.handled = true;
        break;
    case 0x0300:;
        /* Distinguish I2C infrastructure (0x0380-0x03FF) from product events */
        uint16_t id_low = (uint16_t)(event->id & 0x00FF);
        if (id_low >= 0x80) {
            result.owner = EVENT_OWNER_INFRASTRUCTURE;
        } else {
            result.owner = EVENT_OWNER_PRODUCT;
        }
        result.handled = true;
        break;
    case 0x0400:
        result.owner = EVENT_OWNER_CONFIG_STORAGE;
        result.handled = true;
        break;
    case 0x0500:
        result.owner = EVENT_OWNER_TIME_REPORTING;
        result.handled = true;
        break;
    case 0x0600:
        result.owner = EVENT_OWNER_BLE_CELLULAR;
        result.handled = true;
        break;
    case 0x0700:
        result.owner = EVENT_OWNER_DISPLAY_HEALTH;
        result.handled = true;
        break;
    default:
        result.owner = EVENT_OWNER_UNKNOWN;
        result.handled = false;
        break;
    }
    return result;
}

/** @deprecated Use event_mediator_dispatch() for new code */
bool dispatch_to_owner(
    const AppEvent *event,
    EventMediator *mediator,
    SystemModeManager *fsm,
    DataRepository *repo)
{
    if (!event || !fsm) return false;
    return try_mediator_then_legacy(event, mediator, fsm, repo);
}
