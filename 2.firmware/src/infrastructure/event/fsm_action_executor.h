#ifndef SWFPM_FSM_ACTION_EXECUTOR_H
#define SWFPM_FSM_ACTION_EXECUTOR_H

#include <stdint.h>
#include "app/system_fsm.h"

#define FSM_ACTION_EXECUTOR_SLOT_COUNT 7u

typedef enum {
    FSM_ACTION_EXEC_COMPLETE,
    FSM_ACTION_EXEC_PENDING,
    FSM_ACTION_EXEC_FAILED
} FsmActionExecResult;

typedef FsmActionExecResult (*FsmActionHandler)(void *context,
                                                FsmActionMask action);

typedef struct {
    FsmActionHandler handlers[FSM_ACTION_EXECUTOR_SLOT_COUNT];
    void *contexts[FSM_ACTION_EXECUTOR_SLOT_COUNT];
    uint32_t completed_count;
    uint32_t failed_count;
    uint32_t blocked_count;
} FsmActionExecutor;

void fsm_action_executor_init(FsmActionExecutor *executor);

// action must contain exactly one bit from FsmActionMask.
bool fsm_action_executor_bind(FsmActionExecutor *executor,
                              FsmActionMask action,
                              FsmActionHandler handler,
                              void *context);

// Executes at most max_actions in stable bit order. Only terminally completed
// actions are returned for removal from the FSM pending mask.
uint8_t fsm_action_executor_run(FsmActionExecutor *executor,
                                FsmActionMask pending,
                                uint8_t max_actions,
                                FsmActionMask *completed_out);

#endif /* SWFPM_FSM_ACTION_EXECUTOR_H */
