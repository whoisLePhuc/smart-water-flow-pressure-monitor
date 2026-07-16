/**
 * Event queue unit tests
 * Tests: delivery class ordering, priority, overflow, stale, coalesce
 */

#include "infrastructure/queues/app_event_queue.h"
#include "event/app_event.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static AppEventQueue queue;

typedef struct {
    uint32_t enters;
    uint32_t exits;
    uint32_t isr_enters;
} FakeCriticalSection;

static CriticalSectionState fake_enter(void *context, bool from_isr)
{
    FakeCriticalSection *fake = context;
    fake->enters++;
    if (from_isr) fake->isr_enters++;
    return (CriticalSectionState)0x55u;
}

static void fake_exit(void *context, CriticalSectionState previous,
                      bool from_isr)
{
    FakeCriticalSection *fake = context;
    assert(previous == (CriticalSectionState)0x55u);
    (void)from_isr;
    fake->exits++;
}

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

    /* Fill queue with background events (capped at capacity - reserved_critical = 6) */
    for (int i = 0; i < 6; i++) {
        assert(app_event_queue_post(&queue, &e) == EVENT_POST_OK);
    }

    /* Next background event should get backpressure (background limit reached) */
    EventPostResult r = app_event_queue_post(&queue, &e);
    assert(r == EVENT_POST_BACKPRESSURE);
    PASS();
}

static void test_true_reservation_critical_always_has_slots(void)
{
    setup();
    /* Fill background events up to the background cap (6 with default config) */
    AppEvent bg = make_event(EVT_LCD_REFRESH_REQUESTED, EVENT_PRIO_BACKGROUND, DELIVERY_EDGE);
    for (int i = 0; i < 6; i++) {
        app_event_queue_post(&queue, &bg);
    }

    /* Background should now be backpressured */
    assert(app_event_queue_post(&queue, &bg) == EVENT_POST_BACKPRESSURE);

    /* But critical events must still be accepted (reserved slots) */
    AppEvent crit = make_event(EVT_CRITICAL_ERROR, EVENT_PRIO_CRITICAL, DELIVERY_EDGE);
    assert(app_event_queue_post(&queue, &crit) == EVENT_POST_OK);
    PASS();
}

static void test_critical_overflow_escalation(void)
{
    setup();
    AppEvent e = make_event(EVT_CRITICAL_ERROR, EVENT_PRIO_CRITICAL, DELIVERY_EDGE);

    /* Fill entire queue with critical events */
    for (int i = 0; i < 8; i++) {
        assert(app_event_queue_post(&queue, &e) == EVENT_POST_OK);
    }

    /* Next critical should overflow-escalate (queue is truly full) */
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

static void test_isr_post_uses_bound_critical_section(void)
{
    setup();
    FakeCriticalSection fake = {0};
    CriticalSectionPort port = {
        .context = &fake, .enter = fake_enter, .exit = fake_exit
    };
    assert(app_event_queue_bind_critical_section(&queue, &port));
    AppEvent event = make_event(
        EVT_MAX_IRQ_ASSERTED, EVENT_PRIO_MEASUREMENT, DELIVERY_EDGE);
    assert(app_event_queue_post_from_isr(&queue, &event) == EVENT_POST_OK);
    AppEvent out;
    assert(app_event_queue_try_get(&queue, &out));
    assert(out.id == EVT_MAX_IRQ_ASSERTED);
    assert(fake.enters == 2u && fake.exits == 2u);
    assert(fake.isr_enters == 1u);
    PASS();
}

int main(void)
{
    printf("Event Queue Tests\n");
    printf("─────────────────\n");

    test_same_priority_fifo();
    test_priority_ordering();
    test_overflow_backpressure();
    test_true_reservation_critical_always_has_slots();
    test_critical_overflow_escalation();
    test_coalesce_level();
    test_stale_generation();
    test_empty_queue();
    test_isr_post_uses_bound_critical_section();

    printf("─────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
