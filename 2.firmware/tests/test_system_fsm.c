/**
 * System FSM unit tests
 * Tests: transition table, guard true/false, invariant enforcement,
 *        duplicate/stale, critical priority
 */

#include "system_fsm.h"
#include "app_event.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static SystemModeManager mgr;
static AppEvent event;

static void setup(void)
{
    system_fsm_init(&mgr);
    memset(&event, 0, sizeof(event));
    event.source_generation = 1;  /* Match initial generation */
}

static const ModeGuardContext *all_true(void)
{
    static const ModeGuardContext g = {
        .core_ready = true,
        .flow_readiness_evidence_valid = true,
        .service_ready = true,
        .service_authorized = true,
        .safe_service_boundary = true,
        .safe_to_resume_normal = true,
        .critical_blocker_present = false,
        .wake_sources_armed = true,
        .recovery_can_run = true,
        .return_normal = true,
        .return_service = true,
        .reinitialize_allowed = true,
    };
    return &g;
}

/* ── Tests ─────────────────────────────────────────────── */

static void test_init_to_normal(void)
{
    setup();
    event.id = EVT_INIT_COMPLETED;

    ModeGuardContext g = *all_true();
    FsmDispatchResult r = system_fsm_dispatch(&mgr, &event, &g);
    assert(r == FSM_TRANSITION_COMMITTED);
    assert(system_fsm_get_context(&mgr).current_mode == SYSTEM_MODE_NORMAL);
    PASS();
}

static void test_init_guard_false(void)
{
    setup();
    event.id = EVT_INIT_COMPLETED;

    ModeGuardContext g = *all_true();
    g.core_ready = false;  /* Guard fails */

    FsmDispatchResult r = system_fsm_dispatch(&mgr, &event, &g);
    assert(r == FSM_HANDLED_NO_TRANSITION);  /* No transition, stays INIT */
    assert(system_fsm_get_context(&mgr).current_mode == SYSTEM_MODE_INIT);
    PASS();
}

static void test_error_rejects_events(void)
{
    setup();
    mgr.current_mode = SYSTEM_MODE_ERROR;
    mgr.mode_generation = 2;
    event.source_generation = 2;
    event.id = EVT_INIT_COMPLETED;

    ModeGuardContext g = *all_true();
    FsmDispatchResult r = system_fsm_dispatch(&mgr, &event, &g);
    /* Default policy in ERROR: handled without mode change */
    assert(r == FSM_HANDLED_NO_TRANSITION);
    assert(system_fsm_get_context(&mgr).current_mode == SYSTEM_MODE_ERROR);
    PASS();
}

static void test_normal_to_low_power(void)
{
    setup();
    /* Go to NORMAL first */
    event.id = EVT_INIT_COMPLETED;
    system_fsm_dispatch(&mgr, &event, all_true());

    event.id = EVT_LOW_POWER_REQUEST;
    event.source_generation = system_fsm_get_context(&mgr).mode_generation;
    FsmDispatchResult r = system_fsm_dispatch(&mgr, &event, all_true());
    assert(r == FSM_TRANSITION_COMMITTED);
    assert(system_fsm_get_context(&mgr).current_mode == SYSTEM_MODE_LOW_POWER);
    PASS();
}

static void test_low_power_to_normal_on_wake(void)
{
    setup();
    /* Go to NORMAL */
    event.id = EVT_INIT_COMPLETED;
    system_fsm_dispatch(&mgr, &event, all_true());

    /* Go to LOW_POWER */
    event.id = EVT_LOW_POWER_REQUEST;
    event.source_generation = system_fsm_get_context(&mgr).mode_generation;
    system_fsm_dispatch(&mgr, &event, all_true());

    /* Wake */
    event.id = EVT_WAKE;
    event.source_generation = system_fsm_get_context(&mgr).mode_generation;
    FsmDispatchResult r = system_fsm_dispatch(&mgr, &event, all_true());
    assert(r == FSM_TRANSITION_COMMITTED);
    assert(system_fsm_get_context(&mgr).current_mode == SYSTEM_MODE_NORMAL);
    PASS();
}

static void test_critical_error_from_normal(void)
{
    setup();
    event.id = EVT_INIT_COMPLETED;
    system_fsm_dispatch(&mgr, &event, all_true());

    event.id = EVT_CRITICAL_ERROR;
    event.source_generation = system_fsm_get_context(&mgr).mode_generation;
    FsmDispatchResult r = system_fsm_dispatch(&mgr, &event, all_true());
    assert(r == FSM_TRANSITION_COMMITTED);
    assert(system_fsm_get_context(&mgr).current_mode == SYSTEM_MODE_ERROR);
    PASS();
}

static void test_stale_completion_rejected(void)
{
    setup();
    /* A system DELIVERY_COMPLETION event with wrong generation must be rejected.
     * We send a completion event (correlation=1, delivery=COMPLETION) to test this. */
    event.id = EVT_INIT_COMPLETED;
    event.delivery = DELIVERY_COMPLETION;  /* Make it a completion event */
    event.correlation_id = 1;
    event.source_generation = 99;  /* Wrong generation (current is 1) */

    ModeGuardContext gs = *all_true();
    FsmDispatchResult r = system_fsm_dispatch(&mgr, &event, &gs);
    assert(r == FSM_STALE_EVENT);
    PASS();
}

static void test_edge_event_not_checked_for_stale(void)
{
    setup();
    /* EDGE events bypass generation check — EVT_INIT_COMPLETED with wrong gen
     * must still be processed because it's not a completion */
    event.id = EVT_INIT_COMPLETED;
    event.delivery = DELIVERY_EDGE;  /* Default, but explicit for clarity */
    event.source_generation = 99;  /* Wrong generation — should be ignored */

    ModeGuardContext gs = *all_true();
    FsmDispatchResult r = system_fsm_dispatch(&mgr, &event, &gs);
    assert(r == FSM_TRANSITION_COMMITTED);  /* Processed normally */
    assert(system_fsm_get_context(&mgr).current_mode == SYSTEM_MODE_NORMAL);
    PASS();
}

static void test_service_to_recovery(void)
{
    setup();
    /* Go to SERVICE via INIT */
    event.id = EVT_SERVICE_REQUEST;
    system_fsm_dispatch(&mgr, &event, all_true());
    assert(system_fsm_get_context(&mgr).current_mode == SYSTEM_MODE_SERVICE);

    /* Recovery request */
    event.id = EVT_SYSTEM_RECOVERY_REQUIRED;
    event.source_generation = system_fsm_get_context(&mgr).mode_generation;
    FsmDispatchResult r = system_fsm_dispatch(&mgr, &event, all_true());
    assert(r == FSM_TRANSITION_COMMITTED);
    assert(system_fsm_get_context(&mgr).current_mode == SYSTEM_MODE_RECOVERY);
    PASS();
}

static void test_recovery_to_error(void)
{
    setup();
    /* Go to RECOVERY */
    event.id = EVT_RECOVERABLE_INIT_FAILURE;
    system_fsm_dispatch(&mgr, &event, all_true());

    event.id = EVT_RECOVERY_FAILED;
    event.source_generation = system_fsm_get_context(&mgr).mode_generation;
    FsmDispatchResult r = system_fsm_dispatch(&mgr, &event, all_true());
    assert(r == FSM_TRANSITION_COMMITTED);
    assert(system_fsm_get_context(&mgr).current_mode == SYSTEM_MODE_ERROR);
    PASS();
}

/* ── Generation domain tests ─────────────────────────── */

static void test_non_system_event_passes_generation_check(void)
{
    setup();
    /* Go to NORMAL first */
    event.id = EVT_INIT_COMPLETED;
    system_fsm_dispatch(&mgr, &event, all_true());

    /* A measurement event with a mismatched generation (99 vs mode gen)
     * must NOT be rejected as stale — it belongs to a different domain */
    event.id = EVT_FLOW_RESULT_READY;
    event.source_generation = 99;

    ModeGuardContext g = *all_true();
    FsmDispatchResult r = system_fsm_dispatch(&mgr, &event, &g);
    /* Should NOT return FSM_STALE_EVENT — non-system events skip domain check */
    assert(r != FSM_STALE_EVENT);
    PASS();
}

static void test_system_completion_with_wrong_generation_rejected(void)
{
    setup();
    event.id = EVT_INIT_COMPLETED;
    event.delivery = DELIVERY_COMPLETION;  /* Must be COMPLETION for stale check */
    event.source_generation = 99;  /* Wrong — current is 1 */

    ModeGuardContext g = *all_true();
    FsmDispatchResult r = system_fsm_dispatch(&mgr, &event, &g);
    assert(r == FSM_STALE_EVENT);
    PASS();
}

static void test_generation_zero_passes_all_checks(void)
{
    setup();
    /* Generation 0 means "not set" — must pass all domain checks */

    /* System event with gen 0: should pass (treated as not set) */
    event.id = EVT_INIT_COMPLETED;
    event.source_generation = 0;
    ModeGuardContext g = *all_true();
    FsmDispatchResult r = system_fsm_dispatch(&mgr, &event, &g);
    assert(r == FSM_TRANSITION_COMMITTED);
    assert(system_fsm_get_context(&mgr).current_mode == SYSTEM_MODE_NORMAL);
    PASS();
}

static void test_scheduler_event_not_checked_by_fsm(void)
{
    setup();
    /* Scheduler events (like EVT_PRESSURE_SAMPLE_DUE) have their own generation.
     * FSM must not validate them — they pass through to the event router. */

    /* Go to NORMAL first */
    event.id = EVT_INIT_COMPLETED;
    system_fsm_dispatch(&mgr, &event, all_true());

    /* Scheduler due event with wrong scheduler generation */
    event.id = EVT_PRESSURE_SAMPLE_DUE;
    event.source_generation = 0xFF;  /* completely different generation domain */

    ModeGuardContext g = *all_true();
    FsmDispatchResult r = system_fsm_dispatch(&mgr, &event, &g);
    /* FSM must not reject — this event gets routed to MeasurementManager */
    assert(r != FSM_STALE_EVENT);
    PASS();
}

int main(void)
{
    printf("System FSM Tests\n");
    printf("────────────────\n");

    test_init_to_normal();
    test_init_guard_false();
    test_error_rejects_events();
    test_normal_to_low_power();
    test_low_power_to_normal_on_wake();
    test_critical_error_from_normal();
    test_stale_completion_rejected();
    test_edge_event_not_checked_for_stale();
    test_service_to_recovery();
    test_recovery_to_error();
    test_non_system_event_passes_generation_check();
    test_system_completion_with_wrong_generation_rejected();
    test_generation_zero_passes_all_checks();
    test_scheduler_event_not_checked_by_fsm();

    printf("────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
