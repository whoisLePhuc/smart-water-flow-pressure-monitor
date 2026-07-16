#include "infrastructure/repositories/repo_transaction.h"
#include "infrastructure/repositories/data_repository.h"
#include "event/app_event.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  TEST: %s ... ", name)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static void test_txn_lifecycle(void)
{
    TEST("txn_lifecycle");

    DataRepository repo;
    data_repository_init(&repo);

    RepoWriteTxn txn;
    txn_init(&txn);

    assert(txn.state == TXN_STATE_INIT);
    assert(txn_begin(&txn, &repo));
    assert(txn.state == TXN_STATE_ACTIVE);

    FlowResult fr;
    memset(&fr, 0, sizeof(fr));
    fr.flow_ul_per_s = 1500;
    fr.meta.purpose = MEAS_PURPOSE_PRODUCTION;
    fr.meta.origin = DATA_ORIGIN_LIVE_DEVICE;
    fr.meta.provenance = PROVENANCE_MEASURED;
    assert(txn_write_flow(&txn, &fr));

    assert(txn_commit(&txn));
    assert(txn.state == TXN_STATE_COMMITTED);

    RuntimeSnapshot snap;
    assert(txn_read_snapshot(&txn, &snap));
    assert(snap.flow.flow_ul_per_s == 1500);

    PASS();
}

static void test_txn_abort(void)
{
    TEST("txn_abort");

    DataRepository repo;
    data_repository_init(&repo);

    RepoWriteTxn txn;
    txn_init(&txn);
    assert(txn_begin(&txn, &repo));

    RuntimeSnapshot snap_before;
    assert(txn_read_snapshot(&txn, &snap_before));

    FlowResult fr;
    memset(&fr, 0, sizeof(fr));
    fr.flow_ul_per_s = 9999;
    txn_write_flow(&txn, &fr);

    txn_abort(&txn);
    assert(txn.state == TXN_STATE_ABORTED);

    RuntimeSnapshot snap_after;
    assert(txn_read_snapshot(&txn, &snap_after));

    assert(snap_after.flow.flow_ul_per_s != 9999);

    PASS();
}

static void test_active_txn_reads_candidate_snapshot(void)
{
    TEST("active_txn_reads_candidate_snapshot");

    DataRepository repo;
    data_repository_init(&repo);

    RepoWriteTxn txn;
    txn_init(&txn);
    assert(txn_begin(&txn, &repo));

    FlowResult flow;
    memset(&flow, 0, sizeof(flow));
    flow.flow_ul_per_s = 4242;
    assert(txn_write_flow(&txn, &flow));

    RuntimeSnapshot candidate;
    assert(txn_read_snapshot(&txn, &candidate));
    assert(candidate.flow.flow_ul_per_s == 4242);

    RuntimeSnapshot published;
    assert(data_repository_snapshot_copy(&repo, &published));
    assert(published.flow.flow_ul_per_s != 4242);
    txn_abort(&txn);

    PASS();
}

static void test_txn_double_commit(void)
{
    TEST("txn_double_commit");

    DataRepository repo;
    data_repository_init(&repo);

    RepoWriteTxn txn;
    txn_init(&txn);
    assert(txn_begin(&txn, &repo));
    assert(txn_commit(&txn));
    assert(!txn_commit(&txn));
    assert(txn.state == TXN_STATE_ERROR);

    PASS();
}

static void test_txn_write_after_commit(void)
{
    TEST("txn_write_after_commit");

    DataRepository repo;
    data_repository_init(&repo);

    RepoWriteTxn txn;
    txn_init(&txn);
    assert(txn_begin(&txn, &repo));
    assert(txn_commit(&txn));

    FlowResult fr;
    memset(&fr, 0, sizeof(fr));
    assert(!txn_write_flow(&txn, &fr));

    PASS();
}

static void test_txn_generation(void)
{
    TEST("txn_generation");

    DataRepository repo;
    data_repository_init(&repo);

    RepoWriteTxn txn;
    txn_init(&txn);

    uint64_t gen_before = repo.snapshot_version;
    assert(txn_begin(&txn, &repo));
    assert(txn_commit(&txn));
    assert(repo.snapshot_version == gen_before + 1);

    PASS();
}

static void test_txn_begin_on_committed(void)
{
    TEST("txn_begin_on_committed");

    DataRepository repo;
    data_repository_init(&repo);

    RepoWriteTxn txn;
    txn_init(&txn);
    assert(txn_begin(&txn, &repo));
    assert(txn_commit(&txn));
    assert(txn_begin(&txn, &repo));
    assert(txn.state == TXN_STATE_ACTIVE);

    PASS();
}

static void test_txn_rejects_parallel_writer(void)
{
    TEST("txn_rejects_parallel_writer");

    DataRepository repo;
    data_repository_init(&repo);

    RepoWriteTxn first;
    RepoWriteTxn second;
    txn_init(&first);
    txn_init(&second);

    assert(txn_begin(&first, &repo));
    assert(!txn_begin(&second, &repo));
    txn_abort(&first);
    assert(txn_begin(&second, &repo));
    txn_abort(&second);

    PASS();
}

static void test_txn_detects_generation_change(void)
{
    TEST("txn_detects_generation_change");

    DataRepository repo;
    data_repository_init(&repo);

    RepoWriteTxn txn;
    txn_init(&txn);
    assert(txn_begin(&txn, &repo));

    repo.snapshot_version++;
    assert(!txn_commit(&txn));
    assert(txn.state == TXN_STATE_ERROR);

    RepoWriteTxn recovery;
    txn_init(&recovery);
    assert(txn_begin(&recovery, &repo));
    txn_abort(&recovery);

    PASS();
}

int main(void)
{
    printf("Repository Transaction Tests\n");
    printf("─────────────────────────────\n");

    test_txn_lifecycle();
    test_txn_abort();
    test_active_txn_reads_candidate_snapshot();
    test_txn_double_commit();
    test_txn_write_after_commit();
    test_txn_generation();
    test_txn_begin_on_committed();
    test_txn_rejects_parallel_writer();
    test_txn_detects_generation_change();

    printf("─────────────────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return (tests_failed == 0) ? 0 : 1;
}
