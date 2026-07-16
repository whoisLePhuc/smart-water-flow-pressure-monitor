/**
 * Scheduler unit tests
 * Tests: one-shot, periodic, anchor tracking, cancel, miss policy
 */

#include "infrastructure/time/scheduler.h"
#include "event/app_event.h"
#include "platform/include/monotonic_clock_port.h"
#include "platform/include/virtual_clock.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static AppEvent events[8];
static Scheduler scheduler;

static void test_one_shot(void)
{
    virtual_clock_set_mode(CLOCK_MODE_VIRTUAL);
    virtual_clock_set(0);
    scheduler_init(&scheduler);

    ScheduleResult r = scheduler_schedule_one_shot(&scheduler,
        1, 1, EVT_PRESSURE_SAMPLE_DUE, 1000, 1, EVENT_PRIO_MEASUREMENT);
    assert(r == SCHEDULE_OK);

    uint8_t n = scheduler_dispatch_due(&scheduler, 500, events, 8);
    assert(n == 0);  /* Not due yet */

    n = scheduler_dispatch_due(&scheduler, 1500, events, 8);
    assert(n == 1);
    assert(events[0].id == EVT_PRESSURE_SAMPLE_DUE);
    assert(events[0].correlation_id == 1);

    /* Should not fire again */
    n = scheduler_dispatch_due(&scheduler, 2000, events, 8);
    assert(n == 0);
    PASS();
}

static void test_periodic_anchor(void)
{
    virtual_clock_set_mode(CLOCK_MODE_VIRTUAL);
    virtual_clock_set(0);
    scheduler_init(&scheduler);

    scheduler_schedule_periodic(&scheduler,
        2, 1, EVT_PRESSURE_SAMPLE_DUE, 1000, 500, 1,
        MISS_POLICY_SKIP, EVENT_PRIO_MEASUREMENT);

    /* Advance to first deadline */
    uint8_t n = scheduler_dispatch_due(&scheduler, 1000, events, 8);
    assert(n == 1);
    assert(events[0].correlation_id == 2);
    assert(scheduler_acknowledge(&scheduler, 2, 1));

    /* Advance to second deadline */
    n = scheduler_dispatch_due(&scheduler, 1500, events, 8);
    assert(n == 1);
    assert(events[0].correlation_id == 2);
    assert(scheduler_acknowledge(&scheduler, 2, 1));

    /* Third */
    n = scheduler_dispatch_due(&scheduler, 2000, events, 8);
    assert(n == 1);
    assert(!scheduler_acknowledge(&scheduler, 2, 99));
    assert(scheduler_acknowledge(&scheduler, 2, 1));

    PASS();
}

static void test_cancel(void)
{
    virtual_clock_set_mode(CLOCK_MODE_VIRTUAL);
    virtual_clock_set(0);
    scheduler_init(&scheduler);

    scheduler_schedule_one_shot(&scheduler, 3, 1, EVT_PRESSURE_SAMPLE_DUE, 1000, 1, EVENT_PRIO_MEASUREMENT);
    ScheduleResult r = scheduler_cancel(&scheduler, 3, 1);
    assert(r == SCHEDULE_CANCELLED);

    uint8_t n = scheduler_dispatch_due(&scheduler, 2000, events, 8);
    assert(n == 0);  /* Cancelled */
    PASS();
}

static void test_cancel_generation_mismatch(void)
{
    virtual_clock_set_mode(CLOCK_MODE_VIRTUAL);
    virtual_clock_set(0);
    scheduler_init(&scheduler);

    scheduler_schedule_one_shot(&scheduler, 4, 1, EVT_PRESSURE_SAMPLE_DUE, 1000, 2, EVENT_PRIO_MEASUREMENT);
    ScheduleResult r = scheduler_cancel(&scheduler, 4, 1);  /* Wrong generation */
    assert(r == SCHEDULE_NOT_FOUND);
    PASS();
}

static void test_invalid_period(void)
{
    scheduler_init(&scheduler);

    ScheduleResult r = scheduler_schedule_periodic(&scheduler,
        5, 1, EVT_PRESSURE_SAMPLE_DUE, 0, 0, 1,
        MISS_POLICY_SKIP, EVENT_PRIO_MEASUREMENT);
    assert(r == SCHEDULE_REJECTED_INVALID_PERIOD);
    PASS();
}

static void test_instances_are_isolated(void)
{
    Scheduler first;
    Scheduler second;
    scheduler_init(&first);
    scheduler_init(&second);

    assert(scheduler_schedule_one_shot(
        &first, 10u, 1u, EVT_PRESSURE_SAMPLE_DUE,
        1000u, 1u, EVENT_PRIO_MEASUREMENT) == SCHEDULE_OK);

    assert(scheduler_dispatch_due(&second, 2000u, events, 8u) == 0u);
    assert(scheduler_dispatch_due(&first, 2000u, events, 8u) == 1u);
    PASS();
}

int main(void)
{
    printf("Scheduler Tests\n");
    printf("───────────────\n");

    test_one_shot();
    test_periodic_anchor();
    test_cancel();
    test_cancel_generation_mismatch();
    test_invalid_period();
    test_instances_are_isolated();

    printf("───────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
