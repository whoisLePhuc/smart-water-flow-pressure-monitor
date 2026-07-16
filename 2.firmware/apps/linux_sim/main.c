/**
 * Linux Simulator — Post-Refactor Demo
 *
 * Uses AppComposition for explicit module wiring.
 * Demonstrates: boot → NORMAL → LOW_POWER → WAKE → ERROR
 */

#include "app_composition.h"
#include "app_event_queue.h"
#include "app_event.h"
#include "platform/include/monotonic_clock_port.h"
#include "platform/include/platform_runtime.h"
#include <stdio.h>
#include <string.h>

static AppComposition comp;

static void print_mode(const char *label, SystemMode mode)
{
    static const char *names[] = {
        "INIT", "NORMAL", "LOW_POWER", "SERVICE", "RECOVERY", "ERROR"
    };
    printf("[%-12s] SystemMode = %s\n", label,
           mode < SYSTEM_MODE_COUNT ? names[mode] : "???");
}

static void post_event(AppComposition *c, EventId id, uint32_t gen)
{
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = id;
    evt.source_id = 1;
    evt.priority = EVENT_PRIO_CRITICAL;
    evt.delivery = DELIVERY_EDGE;
    evt.source_generation = gen;
    evt.monotonic_timestamp_us = monotonic_now_us();
    app_event_queue_post(&c->queue, &evt);
}

int main(void)
{
    printf("=== Post-Refactor Simulator ===\n\n");

    platform_init();

    LoopBudgetConfig budget = {
        .max_events_per_turn = 8,
        .max_service_steps = 4,
        .max_exec_us = 0,
    };
    app_composition_init(&comp, &budget);

    print_mode("After init", system_fsm_get_context(&comp.fsm).current_mode);

    post_event(&comp, EVT_INIT_COMPLETED, system_fsm_get_context(&comp.fsm).mode_generation);
    app_event_loop_run_once(&comp.loop);
    print_mode("After INIT_COMPLETED", system_fsm_get_context(&comp.fsm).current_mode);

    post_event(&comp, EVT_LOW_POWER_REQUEST, system_fsm_get_context(&comp.fsm).mode_generation);
    app_event_loop_run_once(&comp.loop);
    print_mode("LP request (blocked)", system_fsm_get_context(&comp.fsm).current_mode);

    post_event(&comp, EVT_LOW_POWER_REQUEST, system_fsm_get_context(&comp.fsm).mode_generation);
    app_event_loop_run_once(&comp.loop);
    print_mode("LP request (granted)", system_fsm_get_context(&comp.fsm).current_mode);

    post_event(&comp, EVT_WAKE, system_fsm_get_context(&comp.fsm).mode_generation);
    app_event_loop_run_once(&comp.loop);
    print_mode("After WAKE", system_fsm_get_context(&comp.fsm).current_mode);

    post_event(&comp, EVT_CRITICAL_ERROR, system_fsm_get_context(&comp.fsm).mode_generation);
    app_event_loop_run_once(&comp.loop);
    print_mode("After CRITICAL_ERROR", system_fsm_get_context(&comp.fsm).current_mode);

    printf("\n=== Simulation Complete ===\n");
    return 0;
}
