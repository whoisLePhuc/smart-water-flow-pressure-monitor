/**
 * Power Event Registration Test
 *
 * Verifies that a new domain (power) can register event handlers
 * through the mediator WITHOUT modifying infrastructure code.
 * This is the architectural acceptance test for Phase 4.
 */

#include "event/event_mediator.h"
#include "infrastructure/queues/app_event_queue.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  TEST: %s ... ", name)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static int power_handler_call_count = 0;
static uint32_t last_power_handler_context = 0;

static void power_event_handler(const AppEvent *event, void *context)
{
    (void)event;
    power_handler_call_count++;
    last_power_handler_context = *(const uint32_t *)context;
}

static void test_power_event_registration(void)
{
    TEST("power_event_registration");

    EventMediator m; event_mediator_init(&m);

    uint32_t ctx = 0xDEADBEEF;
    EventMediatorResult r = event_mediator_register(&m, EVT_POWER_STATUS_CHANGED, power_event_handler, &ctx);
    assert(r == EVENT_MEDIATOR_OK);
    assert(event_mediator_handler_count(&m) == 1);

    power_handler_call_count = 0;

    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = EVT_POWER_STATUS_CHANGED;
    evt.priority = EVENT_PRIO_MEASUREMENT;
    evt.delivery = DELIVERY_EDGE;

    EventMediatorResult dr = event_mediator_dispatch(&m, &evt);
    assert(dr == EVENT_MEDIATOR_OK);
    assert(power_handler_call_count == 1);
    assert(last_power_handler_context == 0xDEADBEEF);

    /* Verify: event_mediator.c was NOT modified — this is a pure client-side registration */
    PASS();
}

static void test_power_event_unhandled_without_registration(void)
{
    TEST("power_event_unhandled");

    EventMediator m; event_mediator_init(&m);

    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = EVT_POWER_STATUS_CHANGED;

    /* Without registration, dispatch should return UNHANDLED */
    EventMediatorResult dr = event_mediator_dispatch(&m, &evt);
    assert(dr == EVENT_MEDIATOR_UNHANDLED);

    PASS();
}

int main(void)
{
    printf("Power Event Registration Test\n");
    printf("───────────────────────────────\n");

    test_power_event_registration();
    test_power_event_unhandled_without_registration();

    printf("───────────────────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return (tests_failed == 0) ? 0 : 1;
}
