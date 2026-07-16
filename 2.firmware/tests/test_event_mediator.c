/**
 * Event Mediator Contract Tests — Phase 4 Baseline
 *
 * Tests for the handler registration and dispatch mediator.
 * Each test verifies a specific behavior of the mediator API.
 */

#include "event/event_mediator.h"
#include "event/app_event_queue.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  TEST: %s ... ", name)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static AppEvent make_event(EventId id) {
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = id;
    evt.priority = EVENT_PRIO_MEASUREMENT;
    evt.delivery = DELIVERY_EDGE;
    return evt;
}

/* ── Test helpers ── */
static int g_handler_call_count = 0;
static EventId g_handler_event_id = 0;
static void *g_handler_context = NULL;

static void test_handler(const AppEvent *event, void *context) {
    g_handler_call_count++;
    g_handler_event_id = event->id;
    g_handler_context = context;
}

/* ── Test 1: Register and dispatch ── */
static void test_register_and_dispatch(void)
{
    TEST("register_and_dispatch");

    EventMediator m; event_mediator_init(&m);

    int my_context = 42;
    g_handler_call_count = 0;

    EventMediatorResult r = event_mediator_register(&m, EVT_CRITICAL_ERROR, test_handler, &my_context);
    assert(r == EVENT_MEDIATOR_OK);

    AppEvent evt = make_event(EVT_CRITICAL_ERROR);
    r = event_mediator_dispatch(&m, &evt);
    assert(r == EVENT_MEDIATOR_OK);
    assert(g_handler_call_count == 1);
    assert(g_handler_event_id == EVT_CRITICAL_ERROR);
    assert(g_handler_context == &my_context);

    PASS();
}

/* ── Test 2: Table full ── */
static void test_table_full(void)
{
    TEST("table_full");

    EventMediator m; event_mediator_init(&m);

    /* Register 16 handlers (one past capacity) */
    EventMediatorResult r = EVENT_MEDIATOR_OK;
    for (int i = 0; i < EVENT_MEDIATOR_MAX_HANDLERS + 1; i++) {
        r = event_mediator_register(&m, (EventId)(0x0100 + i), test_handler, NULL);
    }
    assert(r == EVENT_MEDIATOR_TABLE_FULL);
    assert(event_mediator_handler_count(&m) == EVENT_MEDIATOR_MAX_HANDLERS);

    PASS();
}

/* ── Test 3: Duplicate registration rejected ── */
static void test_duplicate_rejected(void)
{
    TEST("duplicate_rejected");

    EventMediator m; event_mediator_init(&m);

    EventMediatorResult r1 = event_mediator_register(&m, EVT_INIT_COMPLETED, test_handler, NULL);
    assert(r1 == EVENT_MEDIATOR_OK);

    EventMediatorResult r2 = event_mediator_register(&m, EVT_INIT_COMPLETED, test_handler, NULL);
    assert(r2 == EVENT_MEDIATOR_DUPLICATE);

    PASS();
}

/* ── Test 4: Unhandled event returns UNHANDLED ── */
static void test_unhandled_event(void)
{
    TEST("unhandled_event");

    EventMediator m; event_mediator_init(&m);

    AppEvent evt = make_event(0xFFFF);
    EventMediatorResult r = event_mediator_dispatch(&m, &evt);
    assert(r == EVENT_MEDIATOR_UNHANDLED);

    PASS();
}

/* ── Test 5: Null params rejected ── */
static void test_null_params(void)
{
    TEST("null_params");

    EventMediator m; event_mediator_init(&m);

    EventMediatorResult r1 = event_mediator_register(&m, EVT_WAKE, NULL, NULL);
    assert(r1 == EVENT_MEDIATOR_INVALID_PARAM);

    EventMediatorResult r2 = event_mediator_dispatch(&m, NULL);
    assert(r2 == EVENT_MEDIATOR_INVALID_PARAM);

    PASS();
}

/* ── Test 6: Context pointer preserved ── */
static void test_context_preserved(void)
{
    TEST("context_preserved");

    EventMediator m; event_mediator_init(&m);

    int ctx_a = 100;
    int ctx_b = 200;

    event_mediator_register(&m, EVT_WAKE, test_handler, &ctx_a);
    event_mediator_register(&m, EVT_LOW_POWER_REQUEST, test_handler, &ctx_b);

    AppEvent evt = make_event(EVT_WAKE);
    g_handler_context = NULL;
    event_mediator_dispatch(&m, &evt);
    assert(g_handler_context == &ctx_a);
    assert(g_handler_event_id == EVT_WAKE);

    PASS();
}

int main(void)
{
    printf("Event Mediator Contract Tests\n");
    printf("───────────────────────────────\n");

    test_register_and_dispatch();
    test_table_full();
    test_duplicate_rejected();
    test_unhandled_event();
    test_null_params();
    test_context_preserved();

    printf("───────────────────────────────\n");
    printf("Results: %d passed, %d failed\n",
           tests_passed, tests_failed);

    return (tests_failed == 0) ? 0 : 1;
}
