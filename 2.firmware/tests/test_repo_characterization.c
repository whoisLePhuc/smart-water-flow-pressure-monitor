/**
 * Repository Characterization Tests — Phase 0 Baseline
 *
 * These tests document the CURRENT behavior of the DataRepository.
 * They are characterization tests, NOT correctness tests.
 * If these tests fail during refactoring, it means the repository
 * behavior has changed UNINTENTIONALLY.
 *
 * The double-buffer design:
 *   - Two RuntimeSnapshot buffers, atomic index swap
 *   - accept_*() copies into inactive buffer, marks publish_pending
 *   - publish_if_requested() swaps the active index
 *   - First accept copies active→inactive (implicit begin)
 *   - Token prevents duplicate publication per turn
 *
 * Baseline commit: 780c12b5c3be7362f7d2fbed2741fb290ab46c9d
 */

#include "event/data_repository.h"
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

/* ── Test helpers ── */

static void seed_runtime_snapshot(RuntimeSnapshot *s, int version)
{
    memset(s, 0, sizeof(*s));
    s->schema_version = 1;
    s->snapshot_version = (uint64_t)version;
    s->active_config_version = (uint32_t)version;
}

/* ── Test 1: Atomic Commit Visibility ──
 * After publish, reader sees either the old or the new snapshot —
 * never a partial/in-progress state. */

static void test_commit_atomicity(void)
{
    TEST("commit_atomicity");

    DataRepository repo;
    data_repository_init(&repo);

    // Verify initial state
    SnapshotReadHandle h = data_repository_snapshot_acquire(&repo);
    assert(h.acquired);
    const RuntimeSnapshot *init = snapshot_read_ptr(&h);
    assert(init != NULL);
    assert(init->snapshot_version == 0);
    assert(init->active_config_version == 0);
    data_repository_snapshot_release(&h);

    // Accept flow data and publish
    SourceEventToken tok;
    data_repository_init_token(&tok, EVT_FLOW_RESULT_READY);

    FlowResult fr;
    memset(&fr, 0, sizeof(fr));
    fr.meta.purpose = MEAS_PURPOSE_PRODUCTION;
    fr.meta.origin = DATA_ORIGIN_LIVE_DEVICE;
    fr.meta.provenance = PROVENANCE_MEASURED;
    fr.flow_ul_per_s = 1500;

    DataPublishResult result = data_repository_accept_flow(&repo, &fr, &tok);
    assert(result == PUBLISH_OK);

    data_repository_publish_if_requested(&repo);

    // After publish, reader sees the committed snapshot (version incremented)
    h = data_repository_snapshot_acquire(&repo);
    const RuntimeSnapshot *after = snapshot_read_ptr(&h);
    assert(after->snapshot_version >= 1);
    data_repository_snapshot_release(&h);

    PASS();
}

/* ── Test 2: Abort Preserves Active Snapshot ──
 * When a token rejects a duplicate publish, the active snapshot
 * must remain unchanged. */

static void test_abort_preserves_active(void)
{
    TEST("abort_preserves_active");

    DataRepository repo;
    data_repository_init(&repo);
    seed_runtime_snapshot(&repo.buffers[0], 42);

    SourceEventToken tok;
    data_repository_init_token(&tok, EVT_VOLUME_UPDATED);

    VolumeState vs;
    memset(&vs, 0, sizeof(vs));
    vs.state_version = 100;

    DataPublishResult result = data_repository_accept_volume(&repo, &vs, &tok);
    assert(result == PUBLISH_OK);

    data_repository_publish_if_requested(&repo);

    // Verify snapshot was published (version incremented)
    SnapshotReadHandle h = data_repository_snapshot_acquire(&repo);
    const RuntimeSnapshot *s = snapshot_read_ptr(&h);
    uint64_t v_after_publish = s->snapshot_version;
    assert(v_after_publish > 0);
    data_repository_snapshot_release(&h);

    VolumeState vs2;
    memset(&vs2, 0, sizeof(vs2));
    vs2.state_version = 999;

    DataPublishResult result2 = data_repository_accept_volume(&repo, &vs2, &tok);
    assert(result2 == PUBLISH_REJECTED_STALE);
    // Verify version unchanged since second accept was rejected
    SnapshotReadHandle h2 = data_repository_snapshot_acquire(&repo);
    const RuntimeSnapshot *s2 = snapshot_read_ptr(&h2);
    assert(s2->snapshot_version == v_after_publish);
    data_repository_snapshot_release(&h2);

    PASS();
}

/* ── Test 3: Generation Monotonicity ──
 * Each successful publish must increment the snapshot version.
 * The version must never decrease. */

static void test_generation_monotonic(void)
{
    TEST("generation_monotonic");

    DataRepository repo;
    data_repository_init(&repo);

    SnapshotReadHandle h = data_repository_snapshot_acquire(&repo);
    uint64_t prev_version = snapshot_read_ptr(&h)->snapshot_version;
    data_repository_snapshot_release(&h);

    // Publish 3 times, verify version increases each time
    for (int i = 0; i < 3; i++) {
        SourceEventToken tok;
        data_repository_init_token(&tok, EVT_SNAPSHOT_PUBLISH_REQUESTED);

        VolumeState vs;
        memset(&vs, 0, sizeof(vs));
        vs.state_version = (uint64_t)(100 + i);

        data_repository_accept_volume(&repo, &vs, &tok);
        data_repository_publish_if_requested(&repo);

        h = data_repository_snapshot_acquire(&repo);
        uint64_t curr_version = snapshot_read_ptr(&h)->snapshot_version;
        assert(curr_version > prev_version);
        prev_version = curr_version;
        data_repository_snapshot_release(&h);
    }

    PASS();
}

/* ── Test 4: Reader Handle Remains Valid After External Publish ──
 * A reader holding a SnapshotReadHandle should still see the
 * snapshot that was active at acquisition time, even if another
 * publish happens after acquisition. */

static void test_reader_lifetime(void)
{
    TEST("reader_lifetime");

    DataRepository repo;
    data_repository_init(&repo);

    // Do an initial publish to set version
    SourceEventToken tok;
    data_repository_init_token(&tok, EVT_VOLUME_UPDATED);
    VolumeState vs;
    memset(&vs, 0, sizeof(vs));
    data_repository_accept_volume(&repo, &vs, &tok);
    data_repository_publish_if_requested(&repo);

    // Acquire reader handle BEFORE second publish
    SnapshotReadHandle h_before = data_repository_snapshot_acquire(&repo);
    const RuntimeSnapshot *before = snapshot_read_ptr(&h_before);
    assert(before != NULL);
    uint64_t version_before = before->snapshot_version;

    // Publish new data (different token)
    SourceEventToken tok2;
    data_repository_init_token(&tok2, EVT_FLOW_RESULT_READY);
    FlowResult fr;
    memset(&fr, 0, sizeof(fr));
    fr.meta.purpose = MEAS_PURPOSE_PRODUCTION;
    fr.meta.origin = DATA_ORIGIN_LIVE_DEVICE;
    fr.meta.provenance = PROVENANCE_MEASURED;
    data_repository_accept_flow(&repo, &fr, &tok2);
    data_repository_publish_if_requested(&repo);

    // New reader sees updated version
    SnapshotReadHandle h_after = data_repository_snapshot_acquire(&repo);
    const RuntimeSnapshot *after = snapshot_read_ptr(&h_after);
    assert(after->snapshot_version > version_before);
    data_repository_snapshot_release(&h_after);

    // Original reader STILL sees the old version
    const RuntimeSnapshot *still_before = snapshot_read_ptr(&h_before);
    assert(still_before->snapshot_version == version_before);
    data_repository_snapshot_release(&h_before);

    PASS();
}

/* ── Test 5: Accept Rejected After Commit ──
 * After a publish completes and the token is spent,
 * further accept calls with the same token must be rejected. */

static void test_accept_reject_after_commit(void)
{
    TEST("accept_reject_after_commit");

    DataRepository repo;
    data_repository_init(&repo);

    SourceEventToken tok;
    data_repository_init_token(&tok, EVT_PRESSURE_RESULT_READY);

    PressureResult pr;
    memset(&pr, 0, sizeof(pr));
    pr.pressure_pa = 100000;

    data_repository_accept_pressure(&repo, &pr, &tok);
    data_repository_publish_if_requested(&repo);

    PressureResult pr2;
    memset(&pr2, 0, sizeof(pr2));
    pr2.pressure_pa = 999999;
    DataPublishResult result = data_repository_accept_pressure(&repo, &pr2, &tok);
    assert(result == PUBLISH_REJECTED_STALE);

    PASS();
}

/* ── Test 6: Provenance Guard ──
 * Only PRODUCTION purpose, LIVE_DEVICE origin, MEASURED provenance
 * data is accepted for flow. */

static void test_provenance_guard(void)
{
    TEST("provenance_guard");

    DataRepository repo;
    data_repository_init(&repo);

    SourceEventToken tok;
    data_repository_init_token(&tok, EVT_FLOW_RESULT_READY);

    // Non-production flow should be rejected
    FlowResult fr;
    memset(&fr, 0, sizeof(fr));
    fr.meta.purpose = MEAS_PURPOSE_SERVICE;
    fr.meta.origin = DATA_ORIGIN_LIVE_DEVICE;
    fr.meta.provenance = PROVENANCE_MEASURED;

    DataPublishResult result = data_repository_accept_flow(&repo, &fr, &tok);
    assert(result == PUBLISH_REJECTED_INVALID);

    // Simulated origin should be rejected
    fr.meta.purpose = MEAS_PURPOSE_PRODUCTION;
    fr.meta.origin = DATA_ORIGIN_SIMULATED_DEVICE;
    fr.meta.provenance = PROVENANCE_MEASURED;
    result = data_repository_accept_flow(&repo, &fr, &tok);
    assert(result == PUBLISH_REJECTED_INVALID);

    PASS();
}

/* ── main ── */

int main(void)
{
    printf("Repository Characterization Tests\n");
    printf("──────────────────────────────────\n");

    test_commit_atomicity();
    test_abort_preserves_active();
    test_generation_monotonic();
    test_reader_lifetime();
    test_accept_reject_after_commit();
    test_provenance_guard();

    printf("──────────────────────────────────\n");
    printf("Results: %d passed, %d failed\n",
           tests_passed, tests_failed);

    return (tests_failed == 0) ? 0 : 1;
}
