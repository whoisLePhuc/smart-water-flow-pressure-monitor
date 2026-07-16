#include "app/system_fsm.h"
#include <string.h>


static bool event_is_system_event(EventId id)
{
    return id >= 0x0100 && id <= 0x01FF;
}


typedef struct {
    uint32_t    event_id;
    SystemMode  current_mode;
    SystemMode  next_mode;
    FsmActionMask action;
    uint32_t    guard_required;  /* Bitmask of guard fields that must be true */
} TransitionEntry;

/* Guard bit positions in ModeGuardContext */
#define GUARD_CORE_READY               (1U << 0)
#define GUARD_FLOW_READINESS           (1U << 1)
#define GUARD_SERVICE_READY            (1U << 2)
#define GUARD_SERVICE_AUTHORIZED       (1U << 3)
#define GUARD_SAFE_SERVICE_BOUNDARY    (1U << 4)
#define GUARD_SAFE_RESUME_NORMAL       (1U << 5)
#define GUARD_NO_CRITICAL_BLOCKER      (1U << 6)
#define GUARD_WAKE_ARMED               (1U << 7)
#define GUARD_RECOVERY_CAN_RUN         (1U << 8)
#define GUARD_RETURN_NORMAL            (1U << 9)
#define GUARD_RETURN_SERVICE           (1U << 10)
#define GUARD_REINIT_ALLOWED           (1U << 11)

/* Evaluate guard: all required bits must be true */
static bool check_guards(const ModeGuardContext *g, uint32_t required)
{
    if (required & GUARD_CORE_READY)
        if (!g->core_ready) return false;
    if (required & GUARD_FLOW_READINESS)
        if (!g->flow_readiness_evidence_valid) return false;
    if (required & GUARD_SERVICE_READY)
        if (!g->service_ready) return false;
    if (required & GUARD_SERVICE_AUTHORIZED)
        if (!g->service_authorized) return false;
    if (required & GUARD_SAFE_SERVICE_BOUNDARY)
        if (!g->safe_service_boundary) return false;
    if (required & GUARD_SAFE_RESUME_NORMAL)
        if (!g->safe_to_resume_normal) return false;
    if (required & GUARD_NO_CRITICAL_BLOCKER)
        if (g->critical_blocker_present) return false;
    if (required & GUARD_WAKE_ARMED)
        if (!g->wake_sources_armed) return false;
    if (required & GUARD_RECOVERY_CAN_RUN)
        if (!g->recovery_can_run) return false;
    if (required & GUARD_RETURN_NORMAL)
        if (!g->return_normal) return false;
    if (required & GUARD_RETURN_SERVICE)
        if (!g->return_service) return false;
    if (required & GUARD_REINIT_ALLOWED)
        if (!g->reinitialize_allowed) return false;
    return true;
}


static const TransitionEntry transition_table[] = {
    /* TR-SYS-001 */ { EVT_INIT_COMPLETED,              SYSTEM_MODE_INIT, SYSTEM_MODE_NORMAL,    ACTION_START_NORMAL,  GUARD_CORE_READY },
    /* TR-SYS-002 */ { EVT_SERVICE_REQUEST,             SYSTEM_MODE_INIT, SYSTEM_MODE_SERVICE,   ACTION_ENTER_SERVICE, GUARD_SERVICE_READY | GUARD_SERVICE_AUTHORIZED },
    /* TR-SYS-003 */ { EVT_RECOVERABLE_INIT_FAILURE,    SYSTEM_MODE_INIT, SYSTEM_MODE_RECOVERY,  ACTION_START_RECOVERY, GUARD_RECOVERY_CAN_RUN },
    /* TR-SYS-004 */ { EVT_CRITICAL_INIT_FAILURE,       SYSTEM_MODE_INIT, SYSTEM_MODE_ERROR,     ACTION_ENTER_ERROR,   0 },
    /* TR-SYS-005 */ { 0,                               SYSTEM_MODE_INIT, SYSTEM_MODE_INIT,      ACTION_NONE,          0 }, /* default » defer */

    /* TR-SYS-010 */ { EVT_LOW_POWER_REQUEST,           SYSTEM_MODE_NORMAL, SYSTEM_MODE_LOW_POWER, ACTION_PREPARE_LOW_POWER, GUARD_NO_CRITICAL_BLOCKER | GUARD_WAKE_ARMED },
    /* TR-SYS-011 */ { EVT_LOW_POWER_REQUEST,           SYSTEM_MODE_NORMAL, SYSTEM_MODE_NORMAL,   ACTION_NONE,          0 }, /* blocker — remain */
    /* TR-SYS-012 */ { EVT_SERVICE_REQUEST,             SYSTEM_MODE_NORMAL, SYSTEM_MODE_SERVICE,  ACTION_ENTER_SERVICE, GUARD_SERVICE_AUTHORIZED | GUARD_SAFE_SERVICE_BOUNDARY },
    /* TR-SYS-013 */ { EVT_SYSTEM_RECOVERY_REQUIRED,    SYSTEM_MODE_NORMAL, SYSTEM_MODE_RECOVERY, ACTION_START_RECOVERY, GUARD_RECOVERY_CAN_RUN },
    /* TR-SYS-014 */ { EVT_CRITICAL_ERROR,              SYSTEM_MODE_NORMAL, SYSTEM_MODE_ERROR,    ACTION_ENTER_ERROR,   0 },
    /* TR-SYS-015 */ { EVT_CONNECTIVITY_CHANGED,       SYSTEM_MODE_NORMAL, SYSTEM_MODE_NORMAL,   ACTION_NONE,          0 },
    /* TR-SYS-016 */ { 0,                              SYSTEM_MODE_NORMAL, SYSTEM_MODE_NORMAL,   ACTION_NONE,          0 }, /* default » dispatch */

    /* TR-SYS-020 */ { EVT_WAKE,                        SYSTEM_MODE_LOW_POWER, SYSTEM_MODE_NORMAL,    ACTION_RESUME_NORMAL, 0 },
    /* TR-SYS-021 */ { EVT_SERVICE_REQUEST,             SYSTEM_MODE_LOW_POWER, SYSTEM_MODE_SERVICE,   ACTION_RESUME_NORMAL | ACTION_ENTER_SERVICE, GUARD_SERVICE_AUTHORIZED },
    /* TR-SYS-022 */ { EVT_CRITICAL_ERROR,              SYSTEM_MODE_LOW_POWER, SYSTEM_MODE_ERROR,     ACTION_ENTER_ERROR,   0 },
    /* TR-SYS-023 */ { 0,                               SYSTEM_MODE_LOW_POWER, SYSTEM_MODE_LOW_POWER, ACTION_NONE,          0 }, /* spurious wake */

    /* TR-SYS-030 */ { EVT_SERVICE_EXIT,                SYSTEM_MODE_SERVICE, SYSTEM_MODE_NORMAL,  ACTION_START_NORMAL, GUARD_SAFE_RESUME_NORMAL },
    /* TR-SYS-031 */ { EVT_SYSTEM_RECOVERY_REQUIRED,    SYSTEM_MODE_SERVICE, SYSTEM_MODE_RECOVERY, ACTION_START_RECOVERY, GUARD_RECOVERY_CAN_RUN },
    /* TR-SYS-032 */ { EVT_CRITICAL_ERROR,              SYSTEM_MODE_SERVICE, SYSTEM_MODE_ERROR,   ACTION_ENTER_ERROR,   0 },
    /* TR-SYS-033 */ { 0,                               SYSTEM_MODE_SERVICE, SYSTEM_MODE_SERVICE, ACTION_NONE,          0 }, /* unauthorized → reject */

    /* TR-SYS-040 */ { EVT_RECOVERY_SUCCEEDED,          SYSTEM_MODE_RECOVERY, SYSTEM_MODE_NORMAL,  ACTION_RESUME_NORMAL, GUARD_RETURN_NORMAL },
    /* TR-SYS-041 */ { EVT_RECOVERY_SUCCEEDED,          SYSTEM_MODE_RECOVERY, SYSTEM_MODE_SERVICE, ACTION_ENTER_SERVICE, GUARD_RETURN_SERVICE },
    /* TR-SYS-042 */ { EVT_RECOVERY_FAILED,             SYSTEM_MODE_RECOVERY, SYSTEM_MODE_ERROR,   ACTION_ENTER_ERROR,   0 },
    /* TR-SYS-043 */ { EVT_CRITICAL_ERROR,              SYSTEM_MODE_RECOVERY, SYSTEM_MODE_ERROR,   ACTION_ENTER_ERROR,   0 },
    /* TR-SYS-044 */ { 0,                               SYSTEM_MODE_RECOVERY, SYSTEM_MODE_RECOVERY, ACTION_NONE,         0 }, /* defer affected */

    /* TR-SYS-050 */ { EVT_AUTHORIZED_RECOVERY_REQUEST,  SYSTEM_MODE_ERROR, SYSTEM_MODE_RECOVERY, ACTION_START_RECOVERY, GUARD_RECOVERY_CAN_RUN },
    /* TR-SYS-051 */ { EVT_CONTROLLED_REINITIALIZE,      SYSTEM_MODE_ERROR, SYSTEM_MODE_INIT,     ACTION_REQUEST_RESET, GUARD_REINIT_ALLOWED },
    /* TR-SYS-052 */ { 0,                                SYSTEM_MODE_ERROR, SYSTEM_MODE_ERROR,    ACTION_NONE,          0 }, /* reject */
};

static const uint8_t transition_count =
    sizeof(transition_table) / sizeof(transition_table[0]);


void system_fsm_init(SystemModeManager *manager)
{
    if (!manager)
        return;

    memset(manager, 0, sizeof(*manager));
    manager->current_mode = SYSTEM_MODE_INIT;
    manager->mode_generation = 1;
    manager->transition_sequence = 0;
    manager->pending_actions = ACTION_NONE;
}

FsmDispatchResult system_fsm_dispatch(
    SystemModeManager *manager,
    const AppEvent *event,
    const ModeGuardContext *guards)
{
    if (!manager || !event || !guards)
        return FSM_INVARIANT_FAULT;

    /* Validate system mode enum */
    if (manager->current_mode >= SYSTEM_MODE_COUNT)
        return FSM_INVARIANT_FAULT;

    /* Check stale event — only system DELIVERY_COMPLETION events belong to
     * the mode_generation domain. Edge/level/deadline events are not completions
     * and bypass generation checking — they are always valid requests.
     * Non-system events (scheduler, device, bus) use their own generation domain;
     * their owner is responsible for stale detection. */
    if (event_is_system_event(event->id) && event->delivery == DELIVERY_COMPLETION) {
        if (event->source_generation != 0 &&
            event->source_generation != manager->mode_generation) {
            return FSM_STALE_EVENT;
        }
    }

    /* Search transition table */
    for (uint8_t i = 0; i < transition_count; i++) {
        const TransitionEntry *t = &transition_table[i];

        /* Match current mode */
        if (t->current_mode != manager->current_mode)
            continue;

        /* Match event (0 = wildcard/default entry) */
        if (t->event_id != 0 && t->event_id != event->id)
            continue;

        /* For LOW_POWER_REQUEST with blocker: use TR-SYS-011 (no guards) */
        if (event->id == EVT_LOW_POWER_REQUEST &&
            t->event_id == EVT_LOW_POWER_REQUEST &&
            t->guard_required == 0 &&
            manager->current_mode == SYSTEM_MODE_NORMAL) {
            /* This is the blocker-remain entry — only match if guard fails on TR-SYS-010 */
            /* TR-SYS-010 already failed, so this is correct fallthrough */
        }

        /* Evaluate guards */
        if (!check_guards(guards, t->guard_required))
            continue;

        SystemMode prev_mode = manager->current_mode;

        /* Forbidden transition check */
        if (prev_mode == SYSTEM_MODE_ERROR && t->next_mode == SYSTEM_MODE_NORMAL)
            return FSM_INVARIANT_FAULT;

        /* Same mode = handled without mode change */
        if (prev_mode == t->next_mode)
            return FSM_HANDLED_NO_TRANSITION;

        /* Record transition */
        manager->last_record.previous_mode = prev_mode;
        manager->last_record.new_mode = t->next_mode;
        manager->last_record.event_id = event->id;
        manager->last_record.reason_code = 0;   /* Will be detailed in later phases */
        manager->last_record.requester_id = event->source_id;
        manager->last_record.correlation_id = event->correlation_id;
        manager->last_record.transition_sequence = manager->transition_sequence + 1;
        manager->last_record.mode_generation = manager->mode_generation + 1;
        manager->last_record.guard_snapshot_id = 0;
        manager->last_record.action_mask = t->action;

        /* Apply transition */
        manager->transition_sequence++;
        manager->current_mode = t->next_mode;
        manager->mode_generation++;
        manager->reason_code = 0;
        manager->source_event_id = event->id;
        manager->correlation_id = event->correlation_id;
        manager->pending_actions = t->action;

        return FSM_TRANSITION_COMMITTED;
    }

    /* No transition found — apply unhandled-event policy */
    switch (manager->current_mode) {
    case SYSTEM_MODE_ERROR:
        /* For ERROR, most events are REJECTED */
        return FSM_REJECTED;

    case SYSTEM_MODE_LOW_POWER:
        /* LOW_POWER rejects non-wake events */
        return FSM_DEFERRED;

    case SYSTEM_MODE_SERVICE:
        /* SERVICE rejects unauthorized commands */
        return FSM_REJECTED;

    case SYSTEM_MODE_RECOVERY:
        return FSM_DEFERRED;

    default:
        return FSM_HANDLED_NO_TRANSITION;
    }
}

SystemModeContext system_fsm_get_context(const SystemModeManager *manager)
{
    SystemModeContext ctx;
    memset(&ctx, 0, sizeof(ctx));

    if (!manager)
        return ctx;

    ctx.current_mode = manager->current_mode;
    ctx.mode_generation = manager->mode_generation;
    ctx.transition_sequence = manager->transition_sequence;
    ctx.entered_at_monotonic_us = manager->entered_at_monotonic_us;
    ctx.reason_code = manager->reason_code;
    ctx.source_event_id = manager->source_event_id;
    ctx.correlation_id = manager->correlation_id;
    return ctx;
}

TransitionRecord system_fsm_get_transition_record(const SystemModeManager *manager)
{
    TransitionRecord record;
    memset(&record, 0, sizeof(record));
    if (!manager)
        return record;
    return manager->last_record;
}

FsmActionMask system_fsm_get_pending_actions(const SystemModeManager *manager)
{
    if (!manager)
        return ACTION_NONE;
    return manager->pending_actions;
}

void system_fsm_clear_actions(SystemModeManager *manager)
{
    if (manager)
        manager->pending_actions = ACTION_NONE;
}
