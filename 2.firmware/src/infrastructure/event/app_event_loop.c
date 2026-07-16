#include "app_event_loop.h"
#include "app_event.h"
#include "app_event_router.h"
#include "event/event_mediator.h"
#include "event/mode_guard.h"
#include "event/scheduler.h"
#include "platform/include/monotonic_clock_port.h"
#include "platform/include/system_control_port.h"
#include "platform/include/platform_runtime.h"
#include <string.h>

typedef struct {
    SystemModeManager *fsm;
    DataRepository    *repo;
} FsmHandlerContext;

static void fsm_event_handler(const AppEvent *event, void *context)
{
    FsmHandlerContext *ctx = (FsmHandlerContext *)context;
    if (!ctx || !ctx->fsm || !event) return;

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

    FsmDispatchResult fsm_result = system_fsm_dispatch(ctx->fsm, event, &guards);
    if (fsm_result == FSM_TRANSITION_COMMITTED) {
        SourceEventToken token;
        data_repository_init_token(&token, event->id);
        SystemModeContext mode_ctx = system_fsm_get_context(ctx->fsm);
        data_repository_accept_mode(ctx->repo, &mode_ctx, &token);
    }
}

static void stub_handler(const AppEvent *event, void *context)
{
    (void)event;
    (void)context;
}

static void register_event_handlers(SystemModeManager *fsm, DataRepository *repo)
{
    static FsmHandlerContext fsm_ctx;
    fsm_ctx.fsm = fsm;
    fsm_ctx.repo = repo;

    event_mediator_register(EVT_SYSTEM_START,                fsm_event_handler, &fsm_ctx);
    event_mediator_register(EVT_INIT_COMPLETED,              fsm_event_handler, &fsm_ctx);
    event_mediator_register(EVT_RECOVERABLE_INIT_FAILURE,    fsm_event_handler, &fsm_ctx);
    event_mediator_register(EVT_CRITICAL_INIT_FAILURE,       fsm_event_handler, &fsm_ctx);
    event_mediator_register(EVT_LOW_POWER_REQUEST,           fsm_event_handler, &fsm_ctx);
    event_mediator_register(EVT_WAKE,                        fsm_event_handler, &fsm_ctx);
    event_mediator_register(EVT_SERVICE_REQUEST,             fsm_event_handler, &fsm_ctx);
    event_mediator_register(EVT_SERVICE_EXIT,                fsm_event_handler, &fsm_ctx);
    event_mediator_register(EVT_SYSTEM_RECOVERY_REQUIRED,    fsm_event_handler, &fsm_ctx);
    event_mediator_register(EVT_RECOVERY_SUCCEEDED,          fsm_event_handler, &fsm_ctx);
    event_mediator_register(EVT_RECOVERY_FAILED,             fsm_event_handler, &fsm_ctx);
    event_mediator_register(EVT_CRITICAL_ERROR,              fsm_event_handler, &fsm_ctx);
    event_mediator_register(EVT_AUTHORIZED_RECOVERY_REQUEST, fsm_event_handler, &fsm_ctx);
    event_mediator_register(EVT_CONTROLLED_REINITIALIZE,     fsm_event_handler, &fsm_ctx);

    event_mediator_register(EVT_MAX_IRQ_ASSERTED,     stub_handler, NULL);
    event_mediator_register(EVT_VOLUME_UPDATED,       stub_handler, NULL);
    event_mediator_register(EVT_I2C_TRANSACTION_COMPLETED, stub_handler, NULL);
    event_mediator_register(EVT_CONFIG_CANDIDATE_READY,    stub_handler, NULL);
    event_mediator_register(EVT_RTC_ALARM,             stub_handler, NULL);
    event_mediator_register(EVT_BLE_RX_AVAILABLE,      stub_handler, NULL);
    event_mediator_register(EVT_LCD_REFRESH_REQUESTED, stub_handler, NULL);
    event_mediator_register(EVT_POWER_STATUS_CHANGED,  stub_handler, NULL);
}

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

    event_mediator_init();
    register_event_handlers(fsm, repo);
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
