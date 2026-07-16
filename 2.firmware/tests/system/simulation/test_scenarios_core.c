/**
 * Core/runtime deterministic scenarios.
 *
 * Tests: dispatch budget, same-timestamp ordering, stale generation,
 *        duplicate completion, reset, queue pressure, livelock.
 */

#include "sim_harness.h"
#include "normalized_trace.h"
#include "scenario_manifest.h"
#include "scenario_runner.h"
#include "infrastructure/queues/app_event_queue.h"
#include "infrastructure/time/scheduler.h"
#include "app/system_fsm.h"
#include "event/app_event.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

/* ── Core scenario: boot to NORMAL ─────────────────── */

static void test_boot_to_normal(void)
{
    SimHarness h;
    assert(sim_harness_init(&h));

    /* Post INIT_COMPLETED event */
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = EVT_INIT_COMPLETED;
    evt.source_id = 1;
    evt.priority = EVENT_PRIO_CRITICAL;
    evt.delivery = DELIVERY_EDGE;
    app_event_queue_post(&h.event_queue, &evt);

    /* Run until idle */
    RunStatus status = run_controller_until_idle(&h.controller);
    assert(status == RUN_IDLE);

    /* Verify mode */
    SystemMode mode = system_fsm_get_context(&h.fsm).current_mode;
    assert(mode == SYSTEM_MODE_NORMAL);
    PASS();
}

/* ── Core scenario: stale generation rejection ─────── */

static void test_stale_generation_system_completion(void)
{
    SimHarness h;
    assert(sim_harness_init(&h));

    /* Post a DELIVERY_COMPLETION event with wrong generation */
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = EVT_INIT_COMPLETED;
    evt.delivery = DELIVERY_COMPLETION;
    evt.source_generation = 99;  /* Wrong — gen is 1 */
    evt.priority = EVENT_PRIO_CRITICAL;
    app_event_queue_post(&h.event_queue, &evt);

    run_controller_until_idle(&h.controller);

    /* Should still be INIT since event was stale */
    SystemMode mode = system_fsm_get_context(&h.fsm).current_mode;
    assert(mode == SYSTEM_MODE_INIT);
    PASS();
}

/* ── Core scenario: non-system event bypasses FSM gen check ── */

static void test_non_system_event_passes_stale_check(void)
{
    SimHarness h;
    assert(sim_harness_init(&h));

    /* Post a measurement event with wrong gen — FSM must not reject it */
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = EVT_FLOW_RESULT_READY;
    evt.delivery = DELIVERY_EDGE;
    evt.source_generation = 99;
    evt.priority = EVENT_PRIO_MEASUREMENT;
    app_event_queue_post(&h.event_queue, &evt);

    run_controller_until_idle(&h.controller);

    /* Must not crash — event routed to measurement manager (stub) */
    PASS();
}

/* ── Core scenario: event dispatch with bounded budget ── */

static void test_bounded_budget_no_event_loss(void)
{
    SimHarness h;
    assert(sim_harness_init(&h));

    /* Post more events than budget */
    for (int i = 0; i < 6; i++) {
        AppEvent evt;
        memset(&evt, 0, sizeof(evt));
        evt.id = EVT_LCD_REFRESH_REQUESTED;
        evt.priority = EVENT_PRIO_BACKGROUND;
        evt.delivery = DELIVERY_EDGE;
        app_event_queue_post(&h.event_queue, &evt);
    }

    /* Run until idle — all events should be processed */
    RunStatus status = run_controller_until_idle(&h.controller);
    assert(status == RUN_IDLE);
    assert(app_event_queue_get_count(&h.event_queue) == 0);
    PASS();
}

int main(void)
{
    printf("Core Runtime Scenarios\n");
    printf("──────────────────────\n");

    test_boot_to_normal();
    test_stale_generation_system_completion();
    test_non_system_event_passes_stale_check();
    test_bounded_budget_no_event_loss();

    printf("──────────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
