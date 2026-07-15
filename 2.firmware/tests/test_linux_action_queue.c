/**
 * Scheduled Action Queue unit tests
 */

#include "platform/include/linux_scheduled_action_queue.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static LinuxScheduledAction make_action(uint64_t due_us, uint32_t op_id,
                                         uint32_t gen, ActionClass cls)
{
    LinuxScheduledAction a;
    memset(&a, 0, sizeof(a));
    a.due_us = due_us;
    a.operation_id = op_id;
    a.owner_generation = gen;
    a.action_class = cls;
    return a;
}

static void test_dispatch_in_order(void)
{
    LinuxScheduledActionQueue q;
    action_queue_init(&q);

    LinuxScheduledAction a1 = make_action(300, 1, 1, ACTION_CLASS_SPI_COMPLETION);
    LinuxScheduledAction a2 = make_action(100, 2, 1, ACTION_CLASS_SPI_COMPLETION);
    LinuxScheduledAction a3 = make_action(200, 3, 1, ACTION_CLASS_SPI_COMPLETION);

    action_queue_schedule(&q, &a1);
    action_queue_schedule(&q, &a2);
    action_queue_schedule(&q, &a3);

    LinuxScheduledAction out[4];
    uint16_t n = action_queue_dispatch_due(&q, 300, out, 4);

    assert(n == 3);
    assert(out[0].due_us == 100);
    assert(out[1].due_us == 200);
    assert(out[2].due_us == 300);
    PASS();
}

static void test_cancel_by_generation(void)
{
    LinuxScheduledActionQueue q;
    action_queue_init(&q);

    LinuxScheduledAction a1 = make_action(100, 1, 2, ACTION_CLASS_SPI_COMPLETION);
    action_queue_schedule(&q, &a1);

    /* Cancel with wrong generation */
    bool r = action_queue_cancel(&q, 1, 1);
    assert(r == false);
    assert(q.stale_cancel_count == 1);

    /* Cancel with correct generation */
    r = action_queue_cancel(&q, 1, 2);
    assert(r == true);
    assert(q.cancel_count == 1);

    /* Verify not dispatched */
    LinuxScheduledAction out[2];
    uint16_t n = action_queue_dispatch_due(&q, 200, out, 2);
    assert(n == 0);
    PASS();
}

static void test_full_queue_overflow(void)
{
    LinuxScheduledActionQueue q;
    action_queue_init(&q);

    for (uint32_t i = 0; i < (uint32_t)ACTION_QUEUE_MAX_CAPACITY; i++) {
        LinuxScheduledAction a = make_action(100, i, 1, ACTION_CLASS_SPI_COMPLETION);
        bool ok = action_queue_schedule(&q, &a);
        assert(ok);
    }

    LinuxScheduledAction last = make_action(100, 999, 1, ACTION_CLASS_SPI_COMPLETION);
    bool ok = action_queue_schedule(&q, &last);
    assert(ok == false);
    assert(q.overflow_count == 1);
    PASS();
}

static void test_total_order_same_time(void)
{
    LinuxScheduledActionQueue q;
    action_queue_init(&q);

    LinuxScheduledAction a1 = make_action(100, 1, 1, ACTION_CLASS_SPI_COMPLETION);
    a1.resource_id = (uint32_t)2;
    LinuxScheduledAction a2 = make_action(100, 2, 1, ACTION_CLASS_SPI_COMPLETION);
    a2.resource_id = (uint32_t)1;

    action_queue_schedule(&q, &a1);
    action_queue_schedule(&q, &a2);

    LinuxScheduledAction out[3];
    uint16_t n = action_queue_dispatch_due(&q, 100, out, 3);

    assert(n == 2);
    assert(out[0].resource_id == 1);
    assert(out[1].resource_id == 2);
    PASS();
}

static void test_next_deadline(void)
{
    LinuxScheduledActionQueue q;
    action_queue_init(&q);

    uint64_t dl;
    bool r = action_queue_next_deadline(&q, &dl);
    assert(r == false);

    LinuxScheduledAction a = make_action(500, 1, 1, ACTION_CLASS_SPI_COMPLETION);
    action_queue_schedule(&q, &a);
    r = action_queue_next_deadline(&q, &dl);
    assert(r == true);
    assert(dl == 500);
    PASS();
}

int main(void)
{
    printf("Scheduled Action Queue Tests\n");
    printf("─────────────────────────────\n");

    test_dispatch_in_order();
    test_cancel_by_generation();
    test_full_queue_overflow();
    test_total_order_same_time();
    test_next_deadline();

    printf("─────────────────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
