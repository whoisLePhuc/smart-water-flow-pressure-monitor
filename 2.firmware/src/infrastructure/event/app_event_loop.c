#include "app_event_loop.h"
#include "app_event.h"
#include "app_event_router.h"
#include "event/event_mediator.h"
#include "app/mode_guard.h"
#include "infrastructure/time/scheduler.h"
#include "infrastructure/repositories/repo_transaction.h"
#include "platform/include/monotonic_clock_port.h"
#include "platform/include/system_control_port.h"
#include "platform/include/platform_runtime.h"
#include <string.h>

static void fsm_event_handler(const AppEvent *event, void *context)
{
    AppEventLoop *loop = (AppEventLoop *)context;
    if (!loop || !loop->fsm || !loop->repo || !event) return;

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

    RepoWriteTxn txn;
    txn_init(&txn);
    if (!txn_begin(&txn, loop->repo))
        return;

    FsmDispatchResult fsm_result = system_fsm_dispatch(loop->fsm, event, &guards);
    if (fsm_result == FSM_TRANSITION_COMMITTED) {
        SystemModeContext mode_ctx = system_fsm_get_context(loop->fsm);
        if (!txn_write_mode(&txn, &mode_ctx) || !txn_commit(&txn))
            txn_abort(&txn);
    } else {
        txn_abort(&txn);
    }
}

static bool register_fsm_handler(EventMediator *mediator,
                                 EventId event_id,
                                 AppEventLoop *loop)
{
    return event_mediator_register(mediator, event_id,
                                   fsm_event_handler, loop)
        == EVENT_MEDIATOR_OK;
}

static bool register_event_handlers(EventMediator *mediator, AppEventLoop *loop)
{
    return register_fsm_handler(mediator, EVT_SYSTEM_START, loop)
        && register_fsm_handler(mediator, EVT_INIT_COMPLETED, loop)
        && register_fsm_handler(mediator, EVT_RECOVERABLE_INIT_FAILURE, loop)
        && register_fsm_handler(mediator, EVT_CRITICAL_INIT_FAILURE, loop)
        && register_fsm_handler(mediator, EVT_LOW_POWER_REQUEST, loop)
        && register_fsm_handler(mediator, EVT_WAKE, loop)
        && register_fsm_handler(mediator, EVT_SERVICE_REQUEST, loop)
        && register_fsm_handler(mediator, EVT_SERVICE_EXIT, loop)
        && register_fsm_handler(mediator, EVT_SYSTEM_RECOVERY_REQUIRED, loop)
        && register_fsm_handler(mediator, EVT_RECOVERY_SUCCEEDED, loop)
        && register_fsm_handler(mediator, EVT_RECOVERY_FAILED, loop)
        && register_fsm_handler(mediator, EVT_CRITICAL_ERROR, loop)
        && register_fsm_handler(mediator, EVT_AUTHORIZED_RECOVERY_REQUEST, loop)
        && register_fsm_handler(mediator, EVT_CONTROLLED_REINITIALIZE, loop);
}

/* =================================================================
 * API
 * ================================================================= */

void app_event_loop_init(
    AppEventLoop *loop,
    AppEventQueue *queue,
    SystemModeManager *fsm,
    DataRepository *repo,
    EventMediator *mediator,
    Scheduler *scheduler,
    const LoopBudgetConfig *budget)
{
    if (!loop)
        return;

    memset(loop, 0, sizeof(*loop));
    if (!queue || !fsm || !repo || !scheduler)
        return;
    loop->queue = queue;
    loop->fsm = fsm;
    loop->repo = repo;
    loop->mediator = mediator;
    loop->scheduler = scheduler;

    if (budget) {
        loop->budget = *budget;
    } else {
        loop->budget.max_events_per_turn = LOOP_BUDGET_DEFAULT_MAX_EVENTS;
        loop->budget.max_service_steps    = LOOP_BUDGET_DEFAULT_MAX_STEPS;
        loop->budget.max_exec_us          = LOOP_BUDGET_DEFAULT_MAX_EXEC_US;
    }

    if (loop->mediator) {
        event_mediator_init(loop->mediator);
        if (!register_event_handlers(loop->mediator, loop))
            return;
    }

    loop->initialized = true;
}

void app_event_loop_run_once(AppEventLoop *loop)
{
    if (!loop)
        return;

    if (!loop->initialized)
        return;

    /* 1. Platform poll is handled by RunController when using deterministic mode.
     * In standalone mode, platform_poll is a no-op or external event source. */

    /* 2. Scheduler dispatch — due events are posted to the event queue */
    {
        uint64_t now_us = monotonic_now_us();
        AppEvent due_events[LOOP_BUDGET_DEFAULT_MAX_EVENTS];
        uint8_t due_count = scheduler_dispatch_due(loop->scheduler,
                                                    now_us, due_events,
                                                    LOOP_BUDGET_DEFAULT_MAX_EVENTS);
        for (uint8_t i = 0; i < due_count; i++) {
            app_event_queue_post(loop->queue, &due_events[i]);
        }
    }

    /* 3. Dispatch events one at a time — dequeue, route, process (bounded budget) */
    uint8_t steps = 0;
    while (steps < loop->budget.max_service_steps) {
        AppEvent evt;
        if (!app_event_queue_try_get(loop->queue, &evt))
            break;  /* No more events */

        /* Route and dispatch to the correct owner */
        dispatch_to_owner(&evt, loop->mediator, loop->fsm, loop->repo);
        if (evt.delivery == DELIVERY_DEADLINE) {
            (void)scheduler_acknowledge(loop->scheduler,
                                        (SchedulerJobId)evt.correlation_id,
                                        evt.source_generation);
        }
        steps++;
    }

    /* 4. Execute FSM pending actions */
    {
        FsmActionMask actions = system_fsm_get_pending_actions(loop->fsm);
        if (actions & ACTION_REQUEST_RESET) {
            system_request_reset(0);
        }
        /* Other actions (START_NORMAL, PREPARE_LOW_POWER, etc.) will be
         * connected to their respective service owners in later phases. */
        if (actions != ACTION_NONE) {
            system_fsm_clear_actions(loop->fsm);
        }
    }

}

/* =================================================================
 * Raw run-once for RunController
 * ================================================================= */

void app_event_loop_run_once_raw(AppEventQueue *queue,
                                 SystemModeManager *fsm,
                                 DataRepository *repo,
                                 Scheduler *scheduler)
{
    uint8_t steps = 0;
    while (steps < LOOP_BUDGET_DEFAULT_MAX_STEPS) {
        AppEvent evt;
        if (!app_event_queue_try_get(queue, &evt))
            break;
        dispatch_to_owner(&evt, NULL, fsm, repo);
        if (evt.delivery == DELIVERY_DEADLINE) {
            (void)scheduler_acknowledge(scheduler,
                                        (SchedulerJobId)evt.correlation_id,
                                        evt.source_generation);
        }
        steps++;
    }
}

/* =================================================================
 * API
 * ================================================================= */

bool app_event_loop_is_idle(const AppEventLoop *loop)
{
    if (!loop)
        return true;

    if (!loop->initialized)
        return true;

    return app_event_queue_get_count(loop->queue) == 0;
}
