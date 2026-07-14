/**
 * System FSM unit tests
 * Tests: transition table, guard true/false, invariant enforcement,
 *        duplicate/stale, critical priority
 */

#include "core/system_fsm.h"
#include "core/app_event.h"
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

static void test_stale_event(void)
{
    setup();
    event.id = EVT_INIT_COMPLETED;
    event.source_generation = 99;  /* Wrong generation (current is 1) */

    ModeGuardContext gs = *all_true();
    FsmDispatchResult r = system_fsm_dispatch(&mgr, &event, &gs);
    assert(r == FSM_STALE_EVENT);
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
    test_stale_event();
    test_service_to_recovery();
    test_recovery_to_error();

    printf("────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
