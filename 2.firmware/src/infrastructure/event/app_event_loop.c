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

    ModeGuardContext guards = mode_guard_capture(
        &loop->guard_provider, event, loop->fsm->current_mode);

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

static FsmActionExecResult request_reset_action(void *context,
                                                FsmActionMask action)
{
    (void)context;
    (void)action;
    system_request_reset(0);
    return FSM_ACTION_EXEC_COMPLETE;
}

static bool execution_budget_elapsed(const AppEventLoop *loop,
                                     uint64_t started_us)
{
    if (!loop || loop->budget.max_exec_us == 0u) return false;
    uint64_t now_us = monotonic_now_us();
    return now_us >= started_us &&
           now_us - started_us >= loop->budget.max_exec_us;
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
    mode_guard_init(&loop->guard_provider);
    fsm_action_executor_init(&loop->action_executor);
    (void)fsm_action_executor_bind(&loop->action_executor,
                                   ACTION_REQUEST_RESET,
                                   request_reset_action, loop);

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


    /* These facts are established by successful composition of the portable
     * runtime. Product/service authorization remains false until published by
     * its owner. */
    ModeGuardContext initial_guards;
    memset(&initial_guards, 0, sizeof(initial_guards));
    initial_guards.core_ready = true;
    initial_guards.recovery_can_run = true;
    initial_guards.wake_sources_armed = true;
    initial_guards.safe_service_boundary = true;
    initial_guards.safe_to_resume_normal = true;
    initial_guards.return_normal = true;
    initial_guards.reinitialize_allowed = true;
    mode_guard_publish(&loop->guard_provider, &initial_guards);

    loop->initialized = true;
}

void app_event_loop_publish_guards(AppEventLoop *loop,
                                   const ModeGuardContext *evidence)
{
    if (loop && evidence)
        mode_guard_publish(&loop->guard_provider, evidence);
}

bool app_event_loop_bind_action(AppEventLoop *loop,
                                FsmActionMask action,
                                FsmActionHandler handler,
                                void *context)
{
    return loop && fsm_action_executor_bind(
        &loop->action_executor, action, handler, context);
}

void app_event_loop_run_once(AppEventLoop *loop)
{
    if (!loop)
        return;

    if (!loop->initialized)
        return;

    uint64_t turn_started_us = monotonic_now_us();
    loop->events_processed_last_turn = 0u;
    bool budget_exhausted = false;

    /* 1. Platform poll is handled by RunController when using deterministic mode.
     * In standalone mode, platform_poll is a no-op or external event source. */

    /* 2. Scheduler dispatch — due events are posted to the event queue */
    {
        uint64_t now_us = monotonic_now_us();
        AppEvent due_events[LOOP_BUDGET_DEFAULT_MAX_EVENTS];
        uint8_t due_limit = loop->budget.max_events_per_turn;
        if (due_limit > LOOP_BUDGET_DEFAULT_MAX_EVENTS)
            due_limit = LOOP_BUDGET_DEFAULT_MAX_EVENTS;
        uint8_t due_count = scheduler_dispatch_due(loop->scheduler,
                                                    now_us, due_events,
                                                    due_limit);
        for (uint8_t i = 0; i < due_count; i++) {
            app_event_queue_post(loop->queue, &due_events[i]);
        }
    }

    /* 3. Dispatch events one at a time — dequeue, route, process (bounded budget) */
    uint8_t steps = 0u;
    while (steps < loop->budget.max_service_steps &&
           loop->events_processed_last_turn < loop->budget.max_events_per_turn) {
        if (execution_budget_elapsed(loop, turn_started_us)) {
            budget_exhausted = true;
            break;
        }
        AppEvent evt;
        if (!app_event_queue_try_get(loop->queue, &evt))
            break;  /* No more events */

        /* Route and dispatch to the correct owner */
        dispatch_to_owner_guarded(&evt, loop->mediator, loop->fsm, loop->repo,
                                  &loop->guard_provider);
        if (evt.delivery == DELIVERY_DEADLINE) {
            (void)scheduler_acknowledge(loop->scheduler,
                                        (SchedulerJobId)evt.correlation_id,
                                        evt.source_generation);
        }
        steps++;
        loop->events_processed_last_turn++;
    }

    /* 4. Execute FSM pending actions */
    if (!execution_budget_elapsed(loop, turn_started_us)) {
        FsmActionMask actions = system_fsm_get_pending_actions(loop->fsm);
        uint8_t remaining_steps = loop->budget.max_service_steps > steps
            ? (uint8_t)(loop->budget.max_service_steps - steps) : 0u;
        FsmActionMask completed = ACTION_NONE;
        if (actions != ACTION_NONE && remaining_steps > 0u) {
            (void)fsm_action_executor_run(&loop->action_executor, actions,
                                          remaining_steps, &completed);
            system_fsm_complete_actions(loop->fsm, completed);
        }
    } else {
        budget_exhausted = true;
    }

    if (app_event_queue_get_count(loop->queue) > 0u &&
        (steps >= loop->budget.max_service_steps ||
         loop->events_processed_last_turn >= loop->budget.max_events_per_turn))
        budget_exhausted = true;
    if (budget_exhausted)
        loop->budget_exhaustion_count++;
}


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


bool app_event_loop_is_idle(const AppEventLoop *loop)
{
    if (!loop)
        return true;

    if (!loop->initialized)
        return true;

    return app_event_queue_get_count(loop->queue) == 0;
}
