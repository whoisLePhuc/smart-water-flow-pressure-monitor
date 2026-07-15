#include "core/app_event_router.h"
#include "core/mode_guard.h"
#include "platform/monotonic_clock_port.h"
#include <string.h>

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

bool dispatch_to_owner(
    const AppEvent *event,
    SystemModeManager *fsm,
    DataRepository *repo)
{
    if (!event || !fsm) return false;

    RouteResult route = route_event(event);
    if (!route.handled) return false;

    switch (route.owner) {
    case EVENT_OWNER_SYSTEM_FSM: {
        /* FSM dispatch with guard context from current published state */
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

    case EVENT_OWNER_MEASUREMENT:
    case EVENT_OWNER_PRODUCT:
    case EVENT_OWNER_INFRASTRUCTURE:
    case EVENT_OWNER_CONFIG_STORAGE:
    case EVENT_OWNER_TIME_REPORTING:
    case EVENT_OWNER_BLE_CELLULAR:
    case EVENT_OWNER_DISPLAY_HEALTH:
        /* Phase 1: owner stubs — events are received but not yet processed.
         * In later phases, each owner will have its own dispatch handler. */
        return true;

    default:
        return false;
    }
}
