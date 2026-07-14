/**
 * Data repository unit tests
 * Tests: double-buffer, atomic swap, no mixed snapshot, provenance guard
 */

#include "core/data_repository.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static DataRepository repo;

static void setup(void)
{
    data_repository_init(&repo);
}

static SourceEventToken make_token(EventId id)
{
    SourceEventToken t;
    data_repository_init_token(&t, id);
    return t;
}

static void test_two_buffers(void)
{
    setup();
    FlowResult flow;
    memset(&flow, 0, sizeof(flow));
    flow.meta.acceptance = DATA_ACCEPTED;
    flow.flow_ul_per_s = 1000;

    SourceEventToken tok = make_token(EVT_FLOW_RESULT_READY);
    DataPublishResult r = data_repository_accept_flow(&repo, &flow, &tok);
    assert(r == PUBLISH_OK);

    assert(data_repository_publish_if_requested(&repo));

    /* Acquire snapshot and verify */
    SnapshotReadHandle h = data_repository_snapshot_acquire(&repo);
    const RuntimeSnapshot *s = snapshot_read_ptr(&h);
    assert(s != NULL);
    assert(s->flow.flow_ul_per_s == 1000);
    data_repository_snapshot_release(&h);
    PASS();
}

static void test_no_mixed_snapshot(void)
{
    setup();

    /* Publish flow */
    FlowResult flow;
    memset(&flow, 0, sizeof(flow));
    flow.meta.acceptance = DATA_ACCEPTED;
    flow.flow_ul_per_s = 1000;
    SourceEventToken tok1 = make_token(EVT_FLOW_RESULT_READY);
    data_repository_accept_flow(&repo, &flow, &tok1);
    data_repository_publish_if_requested(&repo);

    /* Acquire snapshot before next publication */
    SnapshotReadHandle h1 = data_repository_snapshot_acquire(&repo);
    const RuntimeSnapshot *s1 = snapshot_read_ptr(&h1);

    /* Publish new flow while holding handle */
    flow.flow_ul_per_s = 2000;
    SourceEventToken tok2 = make_token(EVT_FLOW_RESULT_READY);
    data_repository_accept_flow(&repo, &flow, &tok2);
    data_repository_publish_if_requested(&repo);

    /* Old handle still sees original value (capture-once) */
    assert(s1->flow.flow_ul_per_s == 1000);

    data_repository_snapshot_release(&h1);

    /* New handle sees updated value */
    SnapshotReadHandle h2 = data_repository_snapshot_acquire(&repo);
    const RuntimeSnapshot *s2 = snapshot_read_ptr(&h2);
    assert(s2->flow.flow_ul_per_s == 2000);
    data_repository_snapshot_release(&h2);
    PASS();
}

static void test_provenance_guard(void)
{
    setup();
    FlowResult flow;
    memset(&flow, 0, sizeof(flow));
    flow.meta.acceptance = DATA_REJECTED;  /* Not accepted */

    SourceEventToken tok = make_token(EVT_FLOW_RESULT_READY);
    DataPublishResult r = data_repository_accept_flow(&repo, &flow, &tok);
    assert(r == PUBLISH_REJECTED_INVALID);  /* Should be rejected */
    PASS();
}

static void test_one_snapshot_per_turn(void)
{
    setup();

    /* Two updates with same token — should only produce one snapshot */
    SourceEventToken tok = make_token(EVT_FLOW_RESULT_READY);

    FlowResult f1; memset(&f1, 0, sizeof(f1)); f1.meta.acceptance = DATA_ACCEPTED; f1.flow_ul_per_s = 100;
    assert(data_repository_accept_flow(&repo, &f1, &tok) == PUBLISH_OK);

    FlowResult f2; memset(&f2, 0, sizeof(f2)); f2.meta.acceptance = DATA_ACCEPTED; f2.flow_ul_per_s = 200;
    assert(data_repository_accept_flow(&repo, &f2, &tok) == PUBLISH_OK);

    assert(data_repository_publish_if_requested(&repo));

    SnapshotReadHandle h = data_repository_snapshot_acquire(&repo);
    assert(snapshot_read_ptr(&h)->flow.flow_ul_per_s == 200);  /* Last value */
    data_repository_snapshot_release(&h);

    /* Second publish should have nothing */
    assert(!data_repository_publish_if_requested(&repo));
    PASS();
}

int main(void)
{
    printf("Data Repository Tests\n");
    printf("─────────────────────\n");

    test_two_buffers();
    test_no_mixed_snapshot();
    test_provenance_guard();
    test_one_snapshot_per_turn();

    printf("─────────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
