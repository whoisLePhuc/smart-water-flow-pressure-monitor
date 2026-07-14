#include "core/app_event_loop.h"
#include "core/app_event.h"
#include "platform/monotonic_clock_port.h"
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

    /* 1. Platform poll — let platform inject external events */
    platform_poll();

    /* 2. Collect events from queue (bounded) */
    AppEvent events[LOOP_BUDGET_DEFAULT_MAX_EVENTS];
    uint8_t event_count = 0;

    while (event_count < loop->budget.max_events_per_turn) {
        AppEvent evt;
        if (!app_event_queue_try_get(loop->queue, &evt))
            break;
        events[event_count++] = evt;
    }

    /* 3. Dispatch events to FSM */
    uint8_t steps = 0;
    for (uint8_t i = 0; i < event_count && steps < loop->budget.max_service_steps; i++) {
        /* Update event generation to current FSM generation */
        events[i].source_generation = system_fsm_get_context(loop->fsm).mode_generation;

        /* Capture guard context */
        ModeGuardContext guards;
        memset(&guards, 0, sizeof(guards));

        /* Simple guard: core ready for NORMAL transitions */
        guards.core_ready = true;
        guards.recovery_can_run = true;
        guards.wake_sources_armed = true;
        guards.service_authorized = true;
        guards.safe_service_boundary = true;
        guards.safe_to_resume_normal = true;
        guards.return_normal = true;
        guards.reinitialize_allowed = true;

        /* Dispatch */
        FsmDispatchResult result = system_fsm_dispatch(loop->fsm, &events[i], &guards);
        (void)result;

        /* If transition committed, publish to repository */
        if (result == FSM_TRANSITION_COMMITTED) {
            SourceEventToken token;
            data_repository_init_token(&token, events[i].id);
            SystemModeContext ctx = system_fsm_get_context(loop->fsm);
            data_repository_accept_mode(loop->repo, &ctx, &token);
        }

        steps++;
    }

    /* 4. Publish final snapshot if requested */
    data_repository_publish_if_requested(loop->repo);
}

bool app_event_loop_is_idle(const AppEventLoop *loop)
{
    if (!loop)
        loop = &default_loop;

    if (!loop->initialized)
        return true;

    return app_event_queue_get_count(loop->queue) == 0;
}
