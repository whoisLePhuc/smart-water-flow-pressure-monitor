/* Canonical repository tests: caller-owned snapshots and explicit writes. */
#include "infrastructure/repositories/data_repository.h"
#include "infrastructure/repositories/repo_transaction.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned tests_passed;
#define RUN(test) do { printf("  TEST: %s ... ", #test); test(); puts("PASS"); ++tests_passed; } while (0)

static FlowResult flow_result(int64_t value)
{
    FlowResult result;
    memset(&result, 0, sizeof(result));
    result.flow_ul_per_s = value;
    result.meta.purpose = MEAS_PURPOSE_PRODUCTION;
    result.meta.origin = DATA_ORIGIN_LIVE_DEVICE;
    result.meta.provenance = PROVENANCE_MEASURED;
    result.meta.validity = DATA_VALID;
    result.meta.freshness = DATA_FRESH;
    result.meta.acceptance = DATA_ACCEPTED;
    return result;
}

static void initialized_snapshot_is_empty(void)
{
    DataRepository repo;
    RuntimeSnapshot snapshot;
    data_repository_init(&repo);
    assert(data_repository_snapshot_copy(&repo, &snapshot));
    assert(snapshot.snapshot_version == 0);
    assert(snapshot.flow.flow_ul_per_s == 0);
}

static void transaction_publishes_atomically(void)
{
    DataRepository repo;
    RepoWriteTxn txn;
    RuntimeSnapshot snapshot;
    FlowResult flow = flow_result(1000);
    VolumeState volume;
    memset(&volume, 0, sizeof(volume));
    volume.state_version = 23;
    data_repository_init(&repo);
    txn_init(&txn);
    assert(txn_begin(&txn, &repo));
    assert(txn_write_flow(&txn, &flow));
    assert(txn_write_volume(&txn, &volume));
    assert(data_repository_snapshot_copy(&repo, &snapshot));
    assert(snapshot.snapshot_version == 0);
    assert(snapshot.flow.flow_ul_per_s == 0);
    assert(txn_commit(&txn));
    assert(data_repository_snapshot_copy(&repo, &snapshot));
    assert(snapshot.snapshot_version == 1);
    assert(snapshot.flow.flow_ul_per_s == 1000);
    assert(snapshot.volume.state_version == 23);
}

static void abort_preserves_published_snapshot(void)
{
    DataRepository repo;
    RepoWriteTxn txn;
    RuntimeSnapshot snapshot;
    FlowResult first = flow_result(100);
    FlowResult discarded = flow_result(999);
    data_repository_init(&repo);
    txn_init(&txn);
    assert(txn_begin(&txn, &repo));
    assert(txn_write_flow(&txn, &first));
    assert(txn_commit(&txn));
    assert(txn_begin(&txn, &repo));
    assert(txn_write_flow(&txn, &discarded));
    txn_abort(&txn);
    assert(data_repository_snapshot_copy(&repo, &snapshot));
    assert(snapshot.snapshot_version == 1);
    assert(snapshot.flow.flow_ul_per_s == 100);
}

static void snapshot_copy_is_caller_owned(void)
{
    DataRepository repo;
    RepoWriteTxn txn;
    RuntimeSnapshot old_copy;
    RuntimeSnapshot new_copy;
    FlowResult first = flow_result(10);
    FlowResult second = flow_result(20);
    data_repository_init(&repo);
    txn_init(&txn);
    assert(txn_begin(&txn, &repo));
    assert(txn_write_flow(&txn, &first));
    assert(txn_commit(&txn));
    assert(data_repository_snapshot_copy(&repo, &old_copy));
    assert(txn_begin(&txn, &repo));
    assert(txn_write_flow(&txn, &second));
    assert(txn_commit(&txn));
    assert(data_repository_snapshot_copy(&repo, &new_copy));
    assert(old_copy.flow.flow_ul_per_s == 10);
    assert(old_copy.snapshot_version == 1);
    assert(new_copy.flow.flow_ul_per_s == 20);
    assert(new_copy.snapshot_version == 2);
}

int main(void)
{
    puts("Data Repository Tests");
    RUN(initialized_snapshot_is_empty);
    RUN(transaction_publishes_atomically);
    RUN(abort_preserves_published_snapshot);
    RUN(snapshot_copy_is_caller_owned);
    printf("Results: %u passed\n", tests_passed);
    return 0;
}
