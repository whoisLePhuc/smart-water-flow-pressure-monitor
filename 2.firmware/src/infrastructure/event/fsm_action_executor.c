#include "infrastructure/event/fsm_action_executor.h"
#include <string.h>

static int8_t action_index(FsmActionMask action)
{
    for (uint8_t i = 0u; i < FSM_ACTION_EXECUTOR_SLOT_COUNT; ++i) {
        if (action == (FsmActionMask)(1u << i)) return (int8_t)i;
    }
    return -1;
}

void fsm_action_executor_init(FsmActionExecutor *executor)
{
    if (executor) memset(executor, 0, sizeof(*executor));
}

bool fsm_action_executor_bind(FsmActionExecutor *executor,
                              FsmActionMask action,
                              FsmActionHandler handler,
                              void *context)
{
    int8_t index = action_index(action);
    if (!executor || index < 0 || !handler) return false;
    executor->handlers[(uint8_t)index] = handler;
    executor->contexts[(uint8_t)index] = context;
    return true;
}

uint8_t fsm_action_executor_run(FsmActionExecutor *executor,
                                FsmActionMask pending,
                                uint8_t max_actions,
                                FsmActionMask *completed_out)
{
    if (completed_out) *completed_out = ACTION_NONE;
    if (!executor || max_actions == 0u) return 0u;
    uint8_t attempted = 0u;
    for (uint8_t i = 0u; i < FSM_ACTION_EXECUTOR_SLOT_COUNT &&
                         attempted < max_actions; ++i) {
        FsmActionMask action = (FsmActionMask)(1u << i);
        if ((pending & action) == 0u) continue;
        FsmActionHandler handler = executor->handlers[i];
        if (!handler) {
            executor->blocked_count++;
            continue;
        }
        attempted++;
        FsmActionExecResult result = handler(executor->contexts[i], action);
        if (result == FSM_ACTION_EXEC_COMPLETE) {
            executor->completed_count++;
            if (completed_out) *completed_out |= action;
        } else if (result == FSM_ACTION_EXEC_FAILED) {
            executor->failed_count++;
        }
    }
    return attempted;
}
