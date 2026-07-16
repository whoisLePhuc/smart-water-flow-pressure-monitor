#include "app/mode_guard.h"
#include "app/system_fsm.h"
#include "infrastructure/event/fsm_action_executor.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t calls;
    FsmActionMask last;
} FakeActionOwner;

static FsmActionExecResult complete_action(void *context,
                                           FsmActionMask action)
{
    FakeActionOwner *owner = context;
    owner->calls++;
    owner->last = action;
    return FSM_ACTION_EXEC_COMPLETE;
}

int main(void)
{
    ModeGuardProvider provider;
    mode_guard_init(&provider);
    AppEvent event;
    memset(&event, 0, sizeof(event));
    ModeGuardContext captured = mode_guard_capture(
        &provider, &event, SYSTEM_MODE_INIT);
    assert(!captured.core_ready);

    ModeGuardContext published;
    memset(&published, 0, sizeof(published));
    published.core_ready = true;
    published.recovery_can_run = true;
    mode_guard_publish(&provider, &published);
    captured = mode_guard_capture(&provider, &event, SYSTEM_MODE_INIT);
    assert(captured.core_ready && captured.recovery_can_run);
    assert(captured.readiness_generation == 1u);

    FsmActionExecutor executor;
    fsm_action_executor_init(&executor);
    FakeActionOwner normal = {0};
    assert(fsm_action_executor_bind(
        &executor, ACTION_START_NORMAL, complete_action, &normal));

    FsmActionMask completed = ACTION_NONE;
    FsmActionMask pending = (FsmActionMask)(
        ACTION_START_NORMAL | ACTION_PREPARE_LOW_POWER);
    assert(fsm_action_executor_run(
        &executor, pending, 1u, &completed) == 1u);
    assert(completed == ACTION_START_NORMAL);
    assert(normal.calls == 1u && normal.last == ACTION_START_NORMAL);

    SystemModeManager manager;
    system_fsm_init(&manager);
    manager.pending_actions = pending;
    system_fsm_complete_actions(&manager, completed);
    assert(manager.pending_actions == ACTION_PREPARE_LOW_POWER);

    completed = ACTION_NONE;
    assert(fsm_action_executor_run(
        &executor, manager.pending_actions, 1u, &completed) == 0u);
    assert(completed == ACTION_NONE);
    assert(manager.pending_actions == ACTION_PREPARE_LOW_POWER);
    assert(executor.blocked_count > 0u);

    puts("Runtime Reliability Tests: PASS");
    return 0;
}
