/**
 * Event queue unit tests
 * Tests: delivery class ordering, priority, overflow, stale, coalesce
 */

#include "app_event_queue.h"
#include "app_event.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static AppEventQueue queue;

static void setup(void)
{
    AppEventQueueConfig cfg = {
        .capacity = 8,
        .reserved_critical = 2,
        .reserved_measurement = 2,
    };
    app_event_queue_init(&queue, &cfg);
}

static AppEvent make_event(EventId id, AppEventPriority prio, AppEventDelivery del)
{
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = id;
    evt.priority = prio;
    evt.delivery = del;
    evt.source_id = 1;
    evt.source_generation = 1;
    return evt;
}

/* ── Tests ─────────────────────────────────────────────── */

static void test_same_priority_fifo(void)
{
    setup();
    AppEvent e1 = make_event(EVT_WAKE, EVENT_PRIO_CRITICAL, DELIVERY_EDGE);
    AppEvent e2 = make_event(EVT_INIT_COMPLETED, EVENT_PRIO_CRITICAL, DELIVERY_EDGE);
    e1.sequence = 1; e2.sequence = 2;

    assert(app_event_queue_post(&queue, &e1) == EVENT_POST_OK);
    assert(app_event_queue_post(&queue, &e2) == EVENT_POST_OK);

    AppEvent out;
    assert(app_event_queue_try_get(&queue, &out));
    assert(out.sequence == 1);  /* FIFO for same priority */
    assert(app_event_queue_try_get(&queue, &out));
    assert(out.sequence == 2);
    assert(!app_event_queue_try_get(&queue, &out));
    PASS();
}

static void test_priority_ordering(void)
{
    setup();
    AppEvent e1 = make_event(EVT_LCD_REFRESH_REQUESTED, EVENT_PRIO_BACKGROUND, DELIVERY_EDGE);
    AppEvent e2 = make_event(EVT_CRITICAL_ERROR, EVENT_PRIO_CRITICAL, DELIVERY_EDGE);

    app_event_queue_post(&queue, &e1);
    app_event_queue_post(&queue, &e2);

    AppEvent out;
    assert(app_event_queue_try_get(&queue, &out));
    assert(out.priority == EVENT_PRIO_CRITICAL);  /* Critical first */
    assert(app_event_queue_try_get(&queue, &out));
    assert(out.priority == EVENT_PRIO_BACKGROUND);
    PASS();
}

static void test_overflow_backpressure(void)
{
    setup();
    AppEvent e = make_event(EVT_LCD_REFRESH_REQUESTED, EVENT_PRIO_BACKGROUND, DELIVERY_EDGE);

    /* Fill queue */
    for (int i = 0; i < 8; i++) {
        assert(app_event_queue_post(&queue, &e) == EVENT_POST_OK);
    }

    /* Next background event should get backpressure */
    EventPostResult r = app_event_queue_post(&queue, &e);
    assert(r == EVENT_POST_BACKPRESSURE);
    PASS();
}

static void test_critical_overflow_escalation(void)
{
    setup();
    AppEvent e = make_event(EVT_CRITICAL_ERROR, EVENT_PRIO_CRITICAL, DELIVERY_EDGE);

    /* Fill critical reserved slots */
    for (int i = 0; i < 2; i++) {
        assert(app_event_queue_post(&queue, &e) == EVENT_POST_OK);
    }

    /* Next critical should overflow-escalate */
    EventPostResult r = app_event_queue_post(&queue, &e);
    assert(r == EVENT_POST_OVERFLOW_ESCALATED);
    PASS();
}

static void test_coalesce_level(void)
{
    setup();
    AppEvent e1 = make_event(EVT_LCD_REFRESH_REQUESTED, EVENT_PRIO_BACKGROUND, DELIVERY_LEVEL);
    AppEvent e2 = make_event(EVT_LCD_REFRESH_REQUESTED, EVENT_PRIO_BACKGROUND, DELIVERY_LEVEL);

    assert(app_event_queue_post(&queue, &e1) == EVENT_POST_OK);
    assert(app_event_queue_post(&queue, &e2) == EVENT_POST_COALESCED);  /* Same level coalesced */
    assert(app_event_queue_get_count(&queue) == 1);
    PASS();
}

static void test_stale_generation(void)
{
    setup();
    AppEvent e = make_event(EVT_INIT_COMPLETED, EVENT_PRIO_CRITICAL, DELIVERY_EDGE);
    e.source_generation = 0;  /* Old generation */

    /* Stale detection is the caller's responsibility via app_event_is_stale() */
    assert(app_event_queue_post(&queue, &e) == EVENT_POST_OK);

    AppEvent out;
    assert(app_event_queue_try_get(&queue, &out));
    assert(app_event_is_stale(&out, 1));  /* Current generation is 1 */
    PASS();
}

static void test_empty_queue(void)
{
    setup();
    AppEvent out;
    assert(!app_event_queue_try_get(&queue, &out));
    assert(app_event_queue_get_count(&queue) == 0);
    PASS();
}

int main(void)
{
    printf("Event Queue Tests\n");
    printf("─────────────────\n");

    test_same_priority_fifo();
    test_priority_ordering();
    test_overflow_backpressure();
    test_critical_overflow_escalation();
    test_coalesce_level();
    test_stale_generation();
    test_empty_queue();

    printf("─────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
