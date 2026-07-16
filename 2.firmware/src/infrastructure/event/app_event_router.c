#include "event/app_event_router.h"
#include "event/event_mediator.h"
#include "app/mode_guard.h"
#include "platform/include/monotonic_clock_port.h"
#include "infrastructure/repositories/repo_transaction.h"
#include <string.h>

/* Try mediator first, then fall back to legacy switch */
static bool try_mediator_then_legacy(
    const AppEvent *event,
    EventMediator *mediator,
    SystemModeManager *fsm,
    DataRepository *repo,
    const ModeGuardProvider *guard_provider)
{
    EventMediatorResult mr = event_mediator_dispatch(mediator, event);
    if (mr == EVENT_MEDIATOR_OK)
        return true;

    /* Fall back to legacy byte-range dispatch for unmigrated events */
    RouteResult route = route_event(event);
    if (!route.handled) return false;

    switch (route.owner) {
    case EVENT_OWNER_SYSTEM_FSM: {
        ModeGuardContext guards = mode_guard_capture(
            guard_provider, event, fsm->current_mode);

        RepoWriteTxn txn;
        txn_init(&txn);
        if (!txn_begin(&txn, repo))
            return false;

        FsmDispatchResult fsm_result = system_fsm_dispatch(fsm, event, &guards);
        if (fsm_result == FSM_TRANSITION_COMMITTED) {
            SystemModeContext ctx = system_fsm_get_context(fsm);
            if (!txn_write_mode(&txn, &ctx) || !txn_commit(&txn)) {
                txn_abort(&txn);
                return false;
            }
        } else {
            txn_abort(&txn);
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
    ModeGuardProvider provider;
    mode_guard_init(&provider);
    ModeGuardContext compatibility_evidence;
    memset(&compatibility_evidence, 0, sizeof(compatibility_evidence));
    compatibility_evidence.core_ready = true;
    compatibility_evidence.recovery_can_run = true;
    compatibility_evidence.wake_sources_armed = true;
    compatibility_evidence.service_authorized = true;
    compatibility_evidence.safe_service_boundary = true;
    compatibility_evidence.safe_to_resume_normal = true;
    compatibility_evidence.return_normal = true;
    compatibility_evidence.reinitialize_allowed = true;
    mode_guard_publish(&provider, &compatibility_evidence);
    return try_mediator_then_legacy(event, mediator, fsm, repo, &provider);
}

bool dispatch_to_owner_guarded(
    const AppEvent *event,
    EventMediator *mediator,
    SystemModeManager *fsm,
    DataRepository *repo,
    const ModeGuardProvider *guard_provider)
{
    if (!event || !fsm || !guard_provider) return false;
    return try_mediator_then_legacy(
        event, mediator, fsm, repo, guard_provider);
}
