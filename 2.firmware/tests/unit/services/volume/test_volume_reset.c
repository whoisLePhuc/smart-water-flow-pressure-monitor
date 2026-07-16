/**
 * test_volume_reset.c — Reset/restore anchor tests (VOL-RST-001)
 */
#include "services/volume/volume_accumulator.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int passed = 0, failed = 0;
#define TEST(n)   do { printf("  %-45s ", n); } while(0)
#define PASS()    do { printf("PASS\n"); passed++; } while(0)
#define FAIL(m)   do { printf("FAIL: %s\n", m); failed++; } while(0)

static VolumeConfig test_config = {
    .config_version = 1,
    .maximum_integration_gap_us = 5000000,
    .max_uncheckpointed_volume_ul = 100000,
    .max_interval_s = 3600,
    .min_spacing_s = 60,
};

static FlowResult make_flow(int64_t flow, uint64_t t_us, uint64_t seq)
{
    FlowResult f;
    memset(&f, 0, sizeof(f));
    f.meta.purpose     = MEAS_PURPOSE_PRODUCTION;
    f.meta.origin      = DATA_ORIGIN_LIVE_DEVICE;
    f.meta.provenance  = PROVENANCE_MEASURED;
    f.meta.validity    = DATA_VALID;
    f.meta.freshness   = DATA_FRESH;
    f.meta.acceptance  = DATA_ACCEPTED;
    f.meta.source_generation = 1;
    f.meta.sample_sequence   = seq;
    f.meta.result_version    = 1;
    f.meta.sample_monotonic_us = t_us;
    f.meta.binding.binding_id  = 1;
    f.flow_ul_per_s = flow;
    return f;
}

static void test_clear_anchor_preserves_counters(void)
{
    VolumeAccumulator acc;
    VolumeAccumulator_Init(&acc, &test_config);

    /* Accumulate some volume */
    FlowResult f1 = make_flow(1000, 1000000, 1);
    assert(VolumeAccumulator_Consume(&acc, &f1) == VOLUME_ANCHORED);
    FlowResult f2 = make_flow(1000, 2000000, 2);
    assert(VolumeAccumulator_Consume(&acc, &f2) == VOLUME_OK);

    const VolumeState *before = VolumeAccumulator_GetState(&acc);
    uint64_t vol_before = before->forward_volume_ul;

    /* Clear anchor (simulates boot/restore) */
    VolumeAccumulator_ClearAnchor(&acc);

    const VolumeState *after = VolumeAccumulator_GetState(&acc);
    if (after->forward_volume_ul != vol_before) {
        FAIL("clear anchor should preserve counters");
        return;
    }

    /* Next sample should anchor, not integrate */
    FlowResult f3 = make_flow(1000, 3000000, 3);
    VolumeConsumeStatus s = VolumeAccumulator_Consume(&acc, &f3);
    if (s != VOLUME_ANCHORED) { FAIL("expected ANCHORED after clear"); return; }

    PASS();
}

static void test_restore_preserves_counters(void)
{
    VolumeAccumulator acc;
    VolumeAccumulator_Init(&acc, &test_config);

    /* Accumulate */
    FlowResult f1 = make_flow(1000, 1000000, 1);
    assert(VolumeAccumulator_Consume(&acc, &f1) == VOLUME_ANCHORED);
    FlowResult f2 = make_flow(1000, 2000000, 2);
    assert(VolumeAccumulator_Consume(&acc, &f2) == VOLUME_OK);

    const VolumeState *st = VolumeAccumulator_GetState(&acc);
    uint64_t saved_forward = st->forward_volume_ul;

    /* Create a restore state */
    VolumeState restored = *st;

    /* Re-init and restore */
    VolumeAccumulator_Init(&acc, &test_config);
    VolumeAccumulator_Restore(&acc, &restored);

    const VolumeState *after = VolumeAccumulator_GetState(&acc);
    if (after->forward_volume_ul != saved_forward) {
        FAIL("restore should preserve counters");
        return;
    }

    /* First sample after restore must anchor, not integrate */
    FlowResult f3 = make_flow(1000, 3000000, 3);
    VolumeConsumeStatus s = VolumeAccumulator_Consume(&acc, &f3);
    if (s != VOLUME_ANCHORED) { FAIL("expected ANCHORED after restore"); return; }

    PASS();
}

int main(void)
{
    printf("Volume Reset Tests\n");
    printf("──────────────────\n");
    test_clear_anchor_preserves_counters();
    test_restore_preserves_counters();
    printf("──────────────────\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
