#ifndef SWFPM_APP_EVENT_LOOP_H
#define SWFPM_APP_EVENT_LOOP_H

#include <stdint.h>
#include <stdbool.h>
#include "infrastructure/queues/app_event_queue.h"
#include "infrastructure/time/scheduler.h"
#include "infrastructure/repositories/data_repository.h"
#include "app/system_fsm.h"
#include "event/event_mediator.h"

/* =================================================================
 * Loop configuration (bounded budgets)
 * ================================================================= */

typedef struct {
    uint8_t  max_events_per_turn;     /* Max events to dispatch in one turn */
    uint8_t  max_service_steps;       /* Max service/action steps per turn */
    uint32_t max_exec_us;             /* Max execution time per turn (0 = unlimited) */
} LoopBudgetConfig;

#define LOOP_BUDGET_DEFAULT_MAX_EVENTS   8
#define LOOP_BUDGET_DEFAULT_MAX_STEPS    4
#define LOOP_BUDGET_DEFAULT_MAX_EXEC_US  0  /* Unlimited (NEEDS_VERIFICATION) */

/* =================================================================
 * Event loop — exposed struct (static allocation only)
 * ================================================================= */

typedef struct {
    AppEventQueue      *queue;
    SystemModeManager  *fsm;
    DataRepository     *repo;
    EventMediator      *mediator;
    Scheduler          *scheduler;
    LoopBudgetConfig    budget;
    bool                initialized;
} AppEventLoop;

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
    const LoopBudgetConfig *budget);

/* Run one complete turn: collect → dispatch → publish → check idle */
void app_event_loop_run_once(AppEventLoop *loop);

/* Check if loop has no pending work */
bool app_event_loop_is_idle(const AppEventLoop *loop);

/* Raw run-once for RunController — takes components directly */
void app_event_loop_run_once_raw(AppEventQueue *queue,
                                 SystemModeManager *fsm,
                                 DataRepository *repo,
                                 Scheduler *scheduler);

#endif /* SWFPM_APP_EVENT_LOOP_H */
