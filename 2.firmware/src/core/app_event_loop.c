#include "app_event_loop.h"
#include "app_event.h"
#include "app_event_router.h"
#include "core/scheduler.h"
#include "platform/monotonic_clock_port.h"
#include "platform/system_control_port.h"
#include "platform/platform_runtime.h"
#include <string.h>

/* =================================================================
 * Default instance
 * ================================================================= */

static AppEventLoop default_loop;

/* =================================================================
 * API
 * ================================================================= */

void app_event_loop_init(
    AppEventLoop *loop,
    AppEventQueue *queue,
    SystemModeManager *fsm,
    DataRepository *repo,
    const LoopBudgetConfig *budget)
{
    if (!loop)
        loop = &default_loop;

    memset(loop, 0, sizeof(*loop));
    loop->queue = queue;
    loop->fsm = fsm;
    loop->repo = repo;

    if (budget) {
        loop->budget = *budget;
    } else {
        loop->budget.max_events_per_turn = LOOP_BUDGET_DEFAULT_MAX_EVENTS;
        loop->budget.max_service_steps    = LOOP_BUDGET_DEFAULT_MAX_STEPS;
        loop->budget.max_exec_us          = LOOP_BUDGET_DEFAULT_MAX_EXEC_US;
    }

    loop->initialized = true;
}

void app_event_loop_run_once(AppEventLoop *loop)
{
    if (!loop)
        loop = &default_loop;

    if (!loop->initialized)
        return;

    /* 1. Platform poll is handled by RunController when using deterministic mode.
     * In standalone mode, platform_poll is a no-op or external event source. */

    /* 2. Scheduler dispatch — due events are posted to the event queue */
    {
        uint64_t now_us = monotonic_now_us();
        AppEvent due_events[LOOP_BUDGET_DEFAULT_MAX_EVENTS];
        uint8_t due_count = scheduler_dispatch_due(now_us, due_events,
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
        dispatch_to_owner(&evt, loop->fsm, loop->repo);
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

    /* 5. Publish final snapshot if requested */
    data_repository_publish_if_requested(loop->repo);
}

/* =================================================================
 * Raw run-once for RunController
 * ================================================================= */

void app_event_loop_run_once_raw(AppEventQueue *queue,
                                 SystemModeManager *fsm,
                                 DataRepository *repo)
{
    uint8_t steps = 0;
    while (steps < LOOP_BUDGET_DEFAULT_MAX_STEPS) {
        AppEvent evt;
        if (!app_event_queue_try_get(queue, &evt))
            break;
        dispatch_to_owner(&evt, fsm, repo);
        steps++;
    }
    data_repository_publish_if_requested(repo);
}

/* =================================================================
 * API
 * ================================================================= */

bool app_event_loop_is_idle(const AppEventLoop *loop)
{
    if (!loop)
        loop = &default_loop;

    if (!loop->initialized)
        return true;

    return app_event_queue_get_count(loop->queue) == 0;
}
