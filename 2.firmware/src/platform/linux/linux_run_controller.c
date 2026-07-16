#include "platform/include/linux_run_controller.h"
#include "event/app_event_loop.h"
#include "event/app_event_router.h"
#include "event/app_event.h"
#include "platform/include/system_control_port.h"
#include <string.h>


static uint64_t compute_signature(RunController *ctrl)
{
    uint64_t sig = 0;
    sig += linux_clock_now_us(ctrl->clock);
    sig += app_event_queue_get_count(ctrl->event_queue);
    sig += action_queue_get_count(ctrl->action_queue);
    sig += (uint64_t)system_fsm_get_context(ctrl->fsm).transition_sequence;
    return sig;
}


void run_controller_init(RunController *ctrl,
                         LinuxVirtualClock *clock,
                         LinuxScheduledActionQueue *action_queue,
                         AppEventQueue *event_queue,
                         Scheduler *scheduler,
                         SystemModeManager *fsm,
                         DataRepository *repo,
                         const RunControllerLimits *limits)
{
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->clock = clock;
    ctrl->action_queue = action_queue;
    ctrl->event_queue = event_queue;
    ctrl->scheduler = scheduler;
    ctrl->fsm = fsm;
    ctrl->repo = repo;

    if (limits) {
        ctrl->limits = *limits;
    } else {
        ctrl->limits.max_turns = RUN_CONTROLLER_DEFAULT_MAX_TURNS;
        ctrl->limits.max_actions_per_turn = RUN_CONTROLLER_DEFAULT_MAX_ACTIONS_PER_TURN;
        ctrl->limits.max_same_time_progress_repeats = RUN_CONTROLLER_DEFAULT_MAX_SAME_TIME_REPEATS;
        ctrl->limits.max_virtual_time_us = RUN_CONTROLLER_DEFAULT_MAX_TIME_US;
    }
}

RunStatus run_controller_one_turn(RunController *ctrl, uint64_t *next_deadline_us)
{
    if (!ctrl) return RUN_ERROR;

    uint64_t now_us = linux_clock_now_us(ctrl->clock);

    /* 1. Dispatch platform actions due at current time */
    LinuxScheduledAction actions[ACTION_QUEUE_MAX_CAPACITY];
    uint16_t num_actions = action_queue_dispatch_due(
        ctrl->action_queue, now_us, actions,
        (uint16_t)ctrl->limits.max_actions_per_turn);

    /* Post action completions as events to the firmware event queue */
    for (uint16_t i = 0; i < num_actions; i++) {
        AppEvent evt;
        memset(&evt, 0, sizeof(evt));
        uint16_t base = 0x0380U;
        uint16_t cls = (uint16_t)actions[i].action_class;
        evt.id = (EventId)(base + cls);
        evt.source_id = actions[i].resource_id;
        evt.priority = EVENT_PRIO_MEASUREMENT;
        evt.delivery = DELIVERY_COMPLETION;
        evt.correlation_id = actions[i].correlation_id;
        evt.source_generation = actions[i].resource_generation;
        evt.monotonic_timestamp_us = now_us;
        app_event_queue_post(ctrl->event_queue, &evt);
    }

    /* 2. Ingest scheduler due events into queue */
    {
        AppEvent due_events[16];
        uint8_t due_count = scheduler_dispatch_due(
            ctrl->scheduler, now_us, due_events, 16);
        for (uint8_t i = 0; i < due_count; i++) {
            app_event_queue_post(ctrl->event_queue, &due_events[i]);
        }
    }

    /* 3. Run bounded firmware event loop turn */
    app_event_loop_run_once_raw(ctrl->event_queue, ctrl->fsm, ctrl->repo,
                                ctrl->scheduler);

    /* 4. Execute pending FSM actions */
    {
        FsmActionMask actions_mask = system_fsm_get_pending_actions(ctrl->fsm);
        if (actions_mask & ACTION_REQUEST_RESET) {
            system_request_reset(0);
        }
        if (actions_mask != ACTION_NONE) {
            system_fsm_clear_actions(ctrl->fsm);
        }
    }

    /* 5. Compute progress and next deadline */
    uint64_t new_sig = compute_signature(ctrl);
    bool made_progress = (new_sig != ctrl->progress_signature);
    ctrl->progress_signature = new_sig;
    ctrl->turn_count++;

    /* Check for work */
    bool has_queue_work = app_event_queue_get_count(ctrl->event_queue) > 0;
    bool has_action_work = action_queue_get_count(ctrl->action_queue) > 0;

    uint64_t sched_dl = 0;
    bool has_sched_work = scheduler_get_next_deadline(ctrl->scheduler,
                                                      &sched_dl);

    if (next_deadline_us) {
        uint64_t act_dl = 0;
        bool has_act_dl = action_queue_next_deadline(ctrl->action_queue, &act_dl);
        if (has_sched_work && has_act_dl) {
            *next_deadline_us = (sched_dl < act_dl) ? sched_dl : act_dl;
        } else if (has_sched_work) {
            *next_deadline_us = sched_dl;
        } else if (has_act_dl) {
            *next_deadline_us = act_dl;
        }
    }

    if (made_progress || has_queue_work || num_actions > 0)
        return RUN_PROGRESS;

    /* Check livelock: same time, same signature, no progress */
    if (now_us == ctrl->last_progress_time_us) {
        ctrl->same_time_progress_count++;
        if (ctrl->same_time_progress_count >= ctrl->limits.max_same_time_progress_repeats) {
            return RUN_LIVELOCK;
        }
    } else {
        ctrl->same_time_progress_count = 0;
        ctrl->last_progress_time_us = now_us;
    }

    if (has_sched_work || has_action_work)
        return RUN_IDLE;  /* Future work exists */

    return RUN_IDLE;
}

RunStatus run_controller_until_idle(RunController *ctrl)
{
    if (!ctrl) return RUN_ERROR;

    uint64_t start_time = linux_clock_now_us(ctrl->clock);
    uint32_t max_turns = ctrl->limits.max_turns;

    for (uint32_t turn = 0; turn < max_turns; turn++) {
        uint64_t next_dl = 0;
        RunStatus status = run_controller_one_turn(ctrl, &next_dl);

        switch (status) {
        case RUN_PROGRESS:
            continue;

        case RUN_LIVELOCK:
            return RUN_LIVELOCK;

        case RUN_ERROR:
            return RUN_ERROR;

        case RUN_IDLE:
            /* Advance to next deadline if any */
            if (next_dl > 0) {
                uint64_t now = linux_clock_now_us(ctrl->clock);
                if (next_dl <= now) {
                    /* Should not happen — means we have due work not dispatched */
                    continue;
                }
                /* Check max virtual time */
                if (next_dl > start_time + ctrl->limits.max_virtual_time_us) {
                    return RUN_IDLE;  /* Beyond simulation horizon */
                }
                linux_clock_advance_to(ctrl->clock, next_dl);
                continue;
            }
            return RUN_IDLE;

        default:
            return status;
        }
    }

    return RUN_STEP_LIMIT;
}
