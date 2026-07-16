#ifndef SWFPM_LINUX_RUN_CONTROLLER_H
#define SWFPM_LINUX_RUN_CONTROLLER_H

#include "platform/include/linux_virtual_clock.h"
#include "platform/include/linux_scheduled_action_queue.h"
#include "infrastructure/queues/app_event_queue.h"
#include "infrastructure/time/scheduler.h"
#include "infrastructure/repositories/data_repository.h"
#include "app/system_fsm.h"

/*
 * Deterministic run controller for Linux simulation.
 *
 * Coordinates virtual clock, scheduled-action queue, firmware event loop,
 * and FSM action execution in a single deterministic turn.
 */

typedef enum {
    RUN_PROGRESS,       /* Work was done in this turn */
    RUN_IDLE,           /* No work pending */
    RUN_STEP_LIMIT,     /* max_turns reached in RunUntilIdle */
    RUN_LIVELOCK,       /* Same time, same progress signature, no advance */
    RUN_ERROR           /* Invariant violation */
} RunStatus;

typedef struct {
    uint32_t max_turns;
    uint32_t max_actions_per_turn;
    uint32_t max_same_time_progress_repeats;
    uint64_t max_virtual_time_us;
} RunControllerLimits;

#define RUN_CONTROLLER_DEFAULT_MAX_TURNS              1000
#define RUN_CONTROLLER_DEFAULT_MAX_ACTIONS_PER_TURN    16
#define RUN_CONTROLLER_DEFAULT_MAX_SAME_TIME_REPEATS     5
#define RUN_CONTROLLER_DEFAULT_MAX_TIME_US        100000000ULL /* 100s virtual */

typedef struct {
    LinuxVirtualClock         *clock;
    LinuxScheduledActionQueue *action_queue;
    AppEventQueue             *event_queue;
    Scheduler                 *scheduler;
    SystemModeManager         *fsm;
    DataRepository            *repo;

    RunControllerLimits        limits;
    uint64_t                   progress_signature;
    uint32_t                   same_time_progress_count;
    uint64_t                   last_progress_time_us;
    uint32_t                   turn_count;
} RunController;

/* ── Lifecycle ─────────────────────────────────────────── */

void run_controller_init(RunController *ctrl,
                         LinuxVirtualClock *clock,
                         LinuxScheduledActionQueue *action_queue,
                         AppEventQueue *event_queue,
                         Scheduler *scheduler,
                         SystemModeManager *fsm,
                         DataRepository *repo,
                         const RunControllerLimits *limits);

/* ── Turn execution ────────────────────────────────────── */

/* Execute one deterministic turn:
 *   1. Dispatch platform actions due at current time
 *   2. Ingest scheduler due events into event queue
 *   3. Run one bounded firmware event loop turn
 *   4. Execute pending FSM actions
 *   5. Publish final snapshot
 *   6. Record progress signature
 * Returns status and optionally next deadline. */
RunStatus run_controller_one_turn(RunController *ctrl,
                                  uint64_t *next_deadline_us);

/* Run turns until idle or limit reached. */
RunStatus run_controller_until_idle(RunController *ctrl);

#endif /* SWFPM_LINUX_RUN_CONTROLLER_H */
