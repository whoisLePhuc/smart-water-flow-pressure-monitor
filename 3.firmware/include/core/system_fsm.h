#ifndef SWFPM_SYSTEM_FSM_H
#define SWFPM_SYSTEM_FSM_H

#include <stdint.h>
#include <stdbool.h>
#include "core/data_model.h"
#include "core/app_event_queue.h"
#include "core/mode_guard.h"

/* =================================================================
 * FSM dispatch result
 * ================================================================= */

typedef enum {
    FSM_TRANSITION_COMMITTED,      /* Mode changed */
    FSM_HANDLED_NO_TRANSITION,     /* Event processed, mode unchanged */
    FSM_DEFERRED,                  /* Event accepted but deferred */
    FSM_REJECTED,                  /* Event rejected in current state */
    FSM_IGNORED_SAFE,              /* Event safely ignored per policy */
    FSM_STALE_EVENT,               /* Generation mismatch */
    FSM_INVARIANT_FAULT            /* FSM invariant violated */
} FsmDispatchResult;

/* =================================================================
 * Transition record
 * ================================================================= */

#define TRANSITION_REASON_MAX 32

typedef struct {
    SystemMode  previous_mode;
    SystemMode  new_mode;
    uint32_t    event_id;
    uint32_t    reason_code;
    uint32_t    requester_id;
    uint64_t    correlation_id;
    uint64_t    transition_sequence;
    uint32_t    mode_generation;
    uint64_t    monotonic_time_us;
    bool        wall_time_valid;
    uint32_t    guard_snapshot_id;
    uint32_t    action_mask;
} TransitionRecord;

/* =================================================================
 * Action tokens — requested by FSM, dispatched by event loop
 * ================================================================= */

typedef enum {
    ACTION_NONE              = 0,
    ACTION_START_NORMAL      = (1U << 0),
    ACTION_ENTER_SERVICE     = (1U << 1),
    ACTION_START_RECOVERY    = (1U << 2),
    ACTION_ENTER_ERROR       = (1U << 3),
    ACTION_PREPARE_LOW_POWER = (1U << 4),
    ACTION_RESUME_NORMAL     = (1U << 5),
    ACTION_REQUEST_RESET     = (1U << 6),
} FsmActionMask;

/* =================================================================
 * FSM manager — exposed struct (static allocation only)
 * ================================================================= */

typedef struct {
    SystemMode       current_mode;
    uint32_t         mode_generation;
    uint64_t         transition_sequence;
    uint64_t         entered_at_monotonic_us;
    uint32_t         reason_code;
    uint32_t         source_event_id;
    uint64_t         correlation_id;
    TransitionRecord last_record;
    FsmActionMask    pending_actions;
} SystemModeManager;

/* =================================================================
 * API
 * ================================================================= */

void system_fsm_init(SystemModeManager *manager);

FsmDispatchResult system_fsm_dispatch(
    SystemModeManager *manager,
    const AppEvent *event,
    const ModeGuardContext *guards);

SystemModeContext system_fsm_get_context(const SystemModeManager *manager);

TransitionRecord system_fsm_get_transition_record(const SystemModeManager *manager);

/* Get the action mask requested by the most recent dispatch */
FsmActionMask system_fsm_get_pending_actions(const SystemModeManager *manager);

/* Clear the action mask after dispatching */
void system_fsm_clear_actions(SystemModeManager *manager);

#endif /* SWFPM_SYSTEM_FSM_H */
