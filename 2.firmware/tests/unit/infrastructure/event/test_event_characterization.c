/**
 * Event Queue Characterization Tests — Phase 0 Baseline
 *
 * These tests document the CURRENT behavior of AppEventQueue
 * and event routing. They are characterization tests, not
 * correctness tests.
 *
 * Event queue properties:
 *   - 5 delivery classes: EDGE, COMPLETION, LEVEL, DEADLINE, MAILBOX
 *   - 5 priority levels: CRITICAL, MEASUREMENT, SHARED_RESOURCE, CONFIG, BACKGROUND
 *   - Priority-ordered dispatch: higher priority first
 *   - FIFO within same priority
 *   - CRITICAL has reserved slots — never silently dropped
 *   - LEVEL delivery coalesces duplicates
 *
 * Baseline commit: 780c12b5c3be7362f7d2fbed2741fb290ab46c9d
 */

#include "infrastructure/queues/app_event_queue.h"
#include "event/app_event.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* ── Test infrastructure ── */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  TEST: %s ... ", name)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

/* ── Helpers ── */

static AppEvent make_event(EventId id, AppEventPriority prio, AppEventDelivery delivery)
{
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = id;
    evt.priority = prio;
    evt.delivery = delivery;
    evt.source_id = 1;
    evt.source_generation = 1;
    evt.monotonic_timestamp_us = 100;
    return evt;
}

/* ── Test 1: FIFO Ordering Within Same Priority ── */

static void test_fifo_ordering(void)
{
    TEST("fifo_ordering");

    AppEventQueue queue;
    AppEventQueueConfig cfg = { .capacity = 8, .reserved_critical = 2, .reserved_measurement = 2 };
    app_event_queue_init(&queue, &cfg);

    // Post 3 events at same priority in order A, B, C
    AppEvent a = make_event(0x0201, EVENT_PRIO_MEASUREMENT, DELIVERY_EDGE);
    a.correlation_id = 1;
    AppEvent b = make_event(0x0202, EVENT_PRIO_MEASUREMENT, DELIVERY_EDGE);
    b.correlation_id = 2;
    AppEvent c = make_event(0x0203, EVENT_PRIO_MEASUREMENT, DELIVERY_EDGE);
    c.correlation_id = 3;

    app_event_queue_post(&queue, &a);
    app_event_queue_post(&queue, &b);
    app_event_queue_post(&queue, &c);

    // Dequeue — must be FIFO order (A, B, C)
    AppEvent out;
    assert(app_event_queue_try_get(&queue, &out));
    assert(out.correlation_id == 1);

    assert(app_event_queue_try_get(&queue, &out));
    assert(out.correlation_id == 2);

    assert(app_event_queue_try_get(&queue, &out));
    assert(out.correlation_id == 3);

    PASS();
}

/* ── Test 2: Priority Strict Ordering ──
 * Higher priority events must be dequeued before lower priority. */

static void test_priority_ordering(void)
{
    TEST("priority_ordering");

    AppEventQueue queue;
    AppEventQueueConfig cfg = { .capacity = 8, .reserved_critical = 2, .reserved_measurement = 2 };
    app_event_queue_init(&queue, &cfg);

    // Post BACKGROUND first, then CRITICAL
    AppEvent bg = make_event(0x0700, EVENT_PRIO_BACKGROUND, DELIVERY_EDGE);
    bg.correlation_id = 99;
    app_event_queue_post(&queue, &bg);

    AppEvent crit = make_event(0x010B, EVENT_PRIO_CRITICAL, DELIVERY_EDGE);
    crit.correlation_id = 1;
    app_event_queue_post(&queue, &crit);

    // CRITICAL must come out first despite being posted second
    AppEvent out;
    assert(app_event_queue_try_get(&queue, &out));
    assert(out.correlation_id == 1);   // CRITICAL
    assert(out.priority == EVENT_PRIO_CRITICAL);

    assert(app_event_queue_try_get(&queue, &out));
    assert(out.correlation_id == 99);  // BACKGROUND
    assert(out.priority == EVENT_PRIO_BACKGROUND);

    PASS();
}

/* ── Test 3: Overflow Backpressure ──
 * When queue is full, post should return backpressure. */

static void test_overflow_behavior(void)
{
    TEST("overflow_behavior");

    AppEventQueue queue;
    AppEventQueueConfig cfg = { .capacity = 4, .reserved_critical = 0, .reserved_measurement = 0 };
    app_event_queue_init(&queue, &cfg);

    // Fill queue with BACKGROUND events
    for (int i = 0; i < 4; i++) {
        AppEvent evt = make_event(0x0700, EVENT_PRIO_BACKGROUND, DELIVERY_EDGE);
        EventPostResult result = app_event_queue_post(&queue, &evt);
        assert(result == EVENT_POST_OK);
    }

    // 5th post should trigger backpressure
    AppEvent evt5 = make_event(0x0700, EVENT_PRIO_BACKGROUND, DELIVERY_EDGE);
    EventPostResult result = app_event_queue_post(&queue, &evt5);
    assert(result == EVENT_POST_BACKPRESSURE);
    assert(app_event_queue_get_count(&queue) == 4);
    PASS();
}

/* ── Test 4: Unknown Event Handling ──
 * Unknown/undefined event IDs should not crash the system. */

static void test_unknown_event_handling(void)
{
    TEST("unknown_event_handling");

    AppEventQueue queue;
    AppEventQueueConfig cfg = {
        .capacity = APP_EVENT_QUEUE_DEFAULT_CAPACITY,
        .reserved_critical = APP_EVENT_QUEUE_DEFAULT_RESERVED_CRITICAL,
        .reserved_measurement = APP_EVENT_QUEUE_DEFAULT_RESERVED_MEASUREMENT
    };
    app_event_queue_init(&queue, &cfg);

    // Post an event with undefined ID (beyond known ranges)
    AppEvent unknown = make_event(0xFFFF, EVENT_PRIO_BACKGROUND, DELIVERY_EDGE);
    EventPostResult result = app_event_queue_post(&queue, &unknown);
    // Should be POST_OK — queue doesn't validate event IDs
    assert(result == EVENT_POST_OK);

    // Dequeue the unknown event — must not crash
    AppEvent out;
    bool got = app_event_queue_try_get(&queue, &out);
    assert(got);
    assert(out.id == 0xFFFF);

    PASS();
}

/* ── Test 5: Empty Queue Returns False ── */

static void test_empty_queue(void)
{
    TEST("empty_queue");

    AppEventQueue queue;
    AppEventQueueConfig cfg = {
        .capacity = APP_EVENT_QUEUE_DEFAULT_CAPACITY,
        .reserved_critical = APP_EVENT_QUEUE_DEFAULT_RESERVED_CRITICAL,
        .reserved_measurement = APP_EVENT_QUEUE_DEFAULT_RESERVED_MEASUREMENT
    };
    app_event_queue_init(&queue, &cfg);

    AppEvent out;
    bool got = app_event_queue_try_get(&queue, &out);
    assert(!got);

    PASS();
}

/* ── Test 6: Payload Lifecycle ──
 * After posting an event, the caller's payload (or referenced data)
 * is independent — modifying the original should not affect the queue. */

static void test_payload_independence(void)
{
    TEST("payload_independence");

    AppEventQueue queue;
    AppEventQueueConfig cfg = {
        .capacity = APP_EVENT_QUEUE_DEFAULT_CAPACITY,
        .reserved_critical = APP_EVENT_QUEUE_DEFAULT_RESERVED_CRITICAL,
        .reserved_measurement = APP_EVENT_QUEUE_DEFAULT_RESERVED_MEASUREMENT
    };
    app_event_queue_init(&queue, &cfg);

    AppEvent evt = make_event(0x0203, EVENT_PRIO_MEASUREMENT, DELIVERY_EDGE);
    evt.payload_size = 4;
    evt.payload[0] = 0xAA;

    app_event_queue_post(&queue, &evt);

    // Modify the original event AFTER posting
    evt.payload[0] = 0xFF;
    evt.payload_size = 8;

    // Dequeue — should see the value that was posted (0xAA), not modified (0xFF)
    AppEvent out;
    assert(app_event_queue_try_get(&queue, &out));
    // The queue copies the entire AppEvent struct including inline payload
    // So payload_size should be 4 (the posted value), not 8
    assert(out.payload_size == 4);
    assert(out.payload[0] == 0xAA);

    PASS();
}

/* ── main ── */

int main(void)
{
    printf("Event Queue Characterization Tests\n");
    printf("─────────────────────────────────────\n");

    test_fifo_ordering();
    test_priority_ordering();
    test_overflow_behavior();
    test_unknown_event_handling();
    test_empty_queue();
    test_payload_independence();

    printf("─────────────────────────────────────\n");
    printf("Results: %d passed, %d failed\n",
           tests_passed, tests_failed);

    return (tests_failed == 0) ? 0 : 1;
}
