/**
 * Linux Simulator — Phase 1 Core Framework Demo
 *
 * Demonstrates: boot → NORMAL → LOW_POWER (blocked) → resolve → LOW_POWER → WAKE
 *
 * Build: see firmware/CMakeLists.txt
 * Run:   ./linux_sim
 */

#include "app_event_queue.h"
#include "app_event_loop.h"
#include "app_event.h"
#include "scheduler.h"
#include "data_repository.h"
#include "system_fsm.h"
#include "platform/monotonic_clock_port.h"
#include "platform/platform_runtime.h"
#include "platform/system_control_port.h"
#include <stdio.h>
#include <string.h>

static AppEventQueue      queue;
static SystemModeManager  fsm;
static DataRepository     repo;
static AppEventLoop       loop;
static bool               storage_busy = true;  /* Simulated blocker */

static void print_mode(const char *label, SystemMode mode)
{
    static const char *names[] = {
        "INIT", "NORMAL", "LOW_POWER", "SERVICE", "RECOVERY", "ERROR"
    };
    printf("[%-12s] SystemMode = %s\n", label,
           mode < SYSTEM_MODE_COUNT ? names[mode] : "???");
}

static void post_event(EventId id, uint32_t gen)
{
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = id;
    evt.source_id = 1;
    evt.priority = EVENT_PRIO_CRITICAL;
    evt.delivery = DELIVERY_EDGE;
    evt.source_generation = gen;
    evt.monotonic_timestamp_us = monotonic_now_us();
    app_event_queue_post(&queue, &evt);
}

int main(void)
{
    printf("=== Phase 1 Core Framework Simulator ===\n\n");

    /* Init all modules */
    platform_init();

    AppEventQueueConfig qcfg = {
        .capacity = 32,
        .reserved_critical = 4,
        .reserved_measurement = 4,
    };
    app_event_queue_init(&queue, &qcfg);
    scheduler_init();
    system_fsm_init(&fsm);
    data_repository_init(&repo);

    LoopBudgetConfig budget = {
        .max_events_per_turn = 8,
        .max_service_steps = 4,
        .max_exec_us = 0,
    };
    app_event_loop_init(&loop, &queue, &fsm, &repo, &budget);

    /* ── Step 1: Boot — INIT ─────────────────────────── */
    print_mode("After init", system_fsm_get_context(&fsm).current_mode);

    /* ── Step 2: INIT → NORMAL ───────────────────────── */
    post_event(EVT_INIT_COMPLETED, system_fsm_get_context(&fsm).mode_generation);
    app_event_loop_run_once(&loop);
    print_mode("After INIT_COMPLETED", system_fsm_get_context(&fsm).current_mode);

    /* ── Step 3: NORMAL → LOW_POWER (blocked) ───────── */
    post_event(EVT_LOW_POWER_REQUEST, system_fsm_get_context(&fsm).mode_generation);
    app_event_loop_run_once(&loop);
    print_mode("LP request (blocked)", system_fsm_get_context(&fsm).current_mode);

    /* ── Step 4: Resolve blocker → LOW_POWER ────────── */
    storage_busy = false;  /* In real firmware, this comes from StorageService */
    post_event(EVT_LOW_POWER_REQUEST, system_fsm_get_context(&fsm).mode_generation);
    app_event_loop_run_once(&loop);
    print_mode("LP request (granted)", system_fsm_get_context(&fsm).current_mode);

    /* ── Step 5: WAKE → NORMAL ───────────────────────── */
    post_event(EVT_WAKE, system_fsm_get_context(&fsm).mode_generation);
    app_event_loop_run_once(&loop);
    print_mode("After WAKE", system_fsm_get_context(&fsm).current_mode);

    /* ── Step 6: CRITICAL_ERROR → ERROR ─────────────── */
    post_event(EVT_CRITICAL_ERROR, system_fsm_get_context(&fsm).mode_generation);
    app_event_loop_run_once(&loop);
    print_mode("After CRITICAL_ERROR", system_fsm_get_context(&fsm).current_mode);

    printf("\n=== Simulation Complete ===\n");
    return 0;
}
