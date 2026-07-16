#ifndef SWFPM_APP_EVENT_LOOP_H
#define SWFPM_APP_EVENT_LOOP_H

#include <stdint.h>
#include <stdbool.h>
#include "infrastructure/queues/app_event_queue.h"
#include "infrastructure/time/scheduler.h"
#include "infrastructure/repositories/data_repository.h"
#include "app/system_fsm.h"
#include "event/event_mediator.h"
#include "app/mode_guard.h"
#include "infrastructure/event/fsm_action_executor.h"


typedef struct {
    uint8_t  max_events_per_turn;     /* Max events to dispatch in one turn */
    uint8_t  max_service_steps;       /* Max service/action steps per turn */
    uint32_t max_exec_us;             /* Max execution time per turn (0 = unlimited) */
} LoopBudgetConfig;

#define LOOP_BUDGET_DEFAULT_MAX_EVENTS   8
#define LOOP_BUDGET_DEFAULT_MAX_STEPS    4
#define LOOP_BUDGET_DEFAULT_MAX_EXEC_US  5000u


typedef struct {
    AppEventQueue *queue;     /* Borrowed; owned by AppComposition. */
    SystemModeManager *fsm;   /* Borrowed; sole owner of primary mode changes. */
    DataRepository *repo;     /* Borrowed; publication goes through transactions. */
    EventMediator *mediator;  /* Borrowed handler registry. */
    Scheduler *scheduler;     /* Borrowed instance-owned monotonic scheduler. */
    ModeGuardProvider guard_provider;
    FsmActionExecutor action_executor;
    LoopBudgetConfig    budget;
    uint8_t             events_processed_last_turn;
    uint32_t            budget_exhaustion_count;
    bool                initialized;
} AppEventLoop;


void app_event_loop_init(
    AppEventLoop *loop,
    AppEventQueue *queue,
    SystemModeManager *fsm,
    DataRepository *repo,
    EventMediator *mediator,
    Scheduler *scheduler,
    const LoopBudgetConfig *budget);

// Executes bounded cooperative work according to budget. Handlers must not
// block; due events that cannot be admitted remain observable through status.
void app_event_loop_run_once(AppEventLoop *loop);

void app_event_loop_publish_guards(AppEventLoop *loop,
                                   const ModeGuardContext *evidence);

bool app_event_loop_bind_action(AppEventLoop *loop,
                                FsmActionMask action,
                                FsmActionHandler handler,
                                void *context);

// Idle means no queued event and no immediately due scheduler work. It does not
// imply that every peripheral is powered down or has no operation in flight.
bool app_event_loop_is_idle(const AppEventLoop *loop);

// Simulation-oriented entry point that bypasses EventMediator. Components are
// borrowed for the duration of the call and retain their normal ownership.
void app_event_loop_run_once_raw(AppEventQueue *queue,
                                 SystemModeManager *fsm,
                                 DataRepository *repo,
                                 Scheduler *scheduler);

#endif /* SWFPM_APP_EVENT_LOOP_H */
