/**
 * Linux simulation integration test
 * Tests: end-to-end scenario with virtual time
 */

#include "app_event_queue.h"
#include "app_event_loop.h"
#include "app_event.h"
#include "scheduler.h"
#include "data_repository.h"
#include "system_fsm.h"
#include "platform/include/monotonic_clock_port.h"
#include "platform/include/platform_runtime.h"
#include "platform/include/virtual_clock.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static AppEventQueue      queue;
static SystemModeManager  fsm;
static DataRepository     repo;
static AppEventLoop       loop;

static void setup(void)
{
    virtual_clock_set_mode(CLOCK_MODE_VIRTUAL);
    virtual_clock_set(0);

    AppEventQueueConfig qcfg = { 32, 4, 4 };
    app_event_queue_init(&queue, &qcfg);
    scheduler_init();
    system_fsm_init(&fsm);
    data_repository_init(&repo);

    LoopBudgetConfig budget = { 8, 4, 0 };
    app_event_loop_init(&loop, &queue, &fsm, &repo, NULL, &budget);
}

static void post_event(EventId id, uint32_t gen)
{
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = id;
    evt.source_id = 1;
    evt.priority = EVENT_PRIO_CRITICAL;
    evt.delivery = DELIVERY_EDGE;
    evt.source_generation = gen;
    evt.monotonic_timestamp_us = monotonic_now_us();
    app_event_queue_post(&queue, &evt);
}

static void test_boot_to_normal(void)
{
    setup();
    post_event(EVT_INIT_COMPLETED, system_fsm_get_context(&fsm).mode_generation);
    app_event_loop_run_once(&loop);
    assert(system_fsm_get_context(&fsm).current_mode == SYSTEM_MODE_NORMAL);
    PASS();
}

static void test_boot_to_error(void)
{
    setup();
    post_event(EVT_CRITICAL_INIT_FAILURE, system_fsm_get_context(&fsm).mode_generation);
    app_event_loop_run_once(&loop);
    assert(system_fsm_get_context(&fsm).current_mode == SYSTEM_MODE_ERROR);
    PASS();
}

static void test_normal_lp_wake(void)
{
    setup();
    /* Boot */
    post_event(EVT_INIT_COMPLETED, system_fsm_get_context(&fsm).mode_generation);
    app_event_loop_run_once(&loop);
    assert(system_fsm_get_context(&fsm).current_mode == SYSTEM_MODE_NORMAL);

    /* Low power request */
    post_event(EVT_LOW_POWER_REQUEST, system_fsm_get_context(&fsm).mode_generation);
    app_event_loop_run_once(&loop);
    assert(system_fsm_get_context(&fsm).current_mode == SYSTEM_MODE_LOW_POWER);

    /* Wake */
    post_event(EVT_WAKE, system_fsm_get_context(&fsm).mode_generation);
    app_event_loop_run_once(&loop);
    assert(system_fsm_get_context(&fsm).current_mode == SYSTEM_MODE_NORMAL);
    PASS();
}

static void test_critical_preempts_lp(void)
{
    setup();
    /* Boot → NORMAL */
    post_event(EVT_INIT_COMPLETED, system_fsm_get_context(&fsm).mode_generation);
    app_event_loop_run_once(&loop);

    /* Post LP request AND critical error */
    post_event(EVT_LOW_POWER_REQUEST, system_fsm_get_context(&fsm).mode_generation);
    post_event(EVT_CRITICAL_ERROR, system_fsm_get_context(&fsm).mode_generation);

    /* Run loop — critical should win */
    app_event_loop_run_once(&loop);
    assert(system_fsm_get_context(&fsm).current_mode == SYSTEM_MODE_ERROR);
    PASS();
}

static void test_snapshot_after_boot(void)
{
    setup();
    post_event(EVT_INIT_COMPLETED, system_fsm_get_context(&fsm).mode_generation);
    app_event_loop_run_once(&loop);

    SnapshotReadHandle h = data_repository_snapshot_acquire(&repo);
    const RuntimeSnapshot *s = snapshot_read_ptr(&h);
    assert(s != NULL);
    assert(s->mode.current_mode == SYSTEM_MODE_NORMAL);
    data_repository_snapshot_release(&h);
    PASS();
}

int main(void)
{
    printf("Linux Simulation Tests\n");
    printf("──────────────────────\n");

    test_boot_to_normal();
    test_boot_to_error();
    test_normal_lp_wake();
    test_critical_preempts_lp();
    test_snapshot_after_boot();

    printf("──────────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
