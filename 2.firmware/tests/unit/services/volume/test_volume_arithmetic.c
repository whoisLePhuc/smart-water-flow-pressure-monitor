/**
 * test_volume_arithmetic.c — Integration arithmetic tests
 * VOL-INT-001 constant flow, VOL-SGN-001 direction, VOL-RND-001 rounding/remainder
 */
#include "services/volume/volume_accumulator.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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

static FlowResult make_flow(int64_t flow_ul_per_s, uint64_t t_us,
                             uint64_t seq, FlowDirection dir)
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
    f.flow_ul_per_s = flow_ul_per_s;
    f.direction     = dir;
    return f;
}

static void test_first_sample_anchors(void)
{
    VolumeAccumulator acc;
    VolumeAccumulator_Init(&acc, &test_config);

    FlowResult f = make_flow(1000, 1000000, 1, FLOW_DIRECTION_FORWARD);
    VolumeConsumeStatus s = VolumeAccumulator_Consume(&acc, &f);
    if (s != VOLUME_ANCHORED) { FAIL("expected ANCHORED"); return; }

    const VolumeState *st = VolumeAccumulator_GetState(&acc);
    if (st->forward_volume_ul != 0) { FAIL("forward should be 0 after anchor"); return; }
    PASS();
}

static void test_constant_forward_integration(void)
{
    VolumeAccumulator acc;
    VolumeAccumulator_Init(&acc, &test_config);

    /* flow = 1000 uL/s, dt = 1 s per sample → 1000 uL per interval */
    FlowResult f1 = make_flow(1000, 1000000, 1, FLOW_DIRECTION_FORWARD);
    assert(VolumeAccumulator_Consume(&acc, &f1) == VOLUME_ANCHORED);

    FlowResult f2 = make_flow(1000, 2000000, 2, FLOW_DIRECTION_FORWARD);
    assert(VolumeAccumulator_Consume(&acc, &f2) == VOLUME_OK);

    const VolumeState *st = VolumeAccumulator_GetState(&acc);
    if (st->forward_volume_ul != 1000) { FAIL("expected 1000 uL forward"); return; }
    PASS();
}

static void test_reverse_integration(void)
{
    VolumeAccumulator acc;
    VolumeAccumulator_Init(&acc, &test_config);

    FlowResult f1 = make_flow(-500, 1000000, 1, FLOW_DIRECTION_REVERSE);
    assert(VolumeAccumulator_Consume(&acc, &f1) == VOLUME_ANCHORED);

    FlowResult f2 = make_flow(-500, 3000000, 2, FLOW_DIRECTION_REVERSE);
    VolumeConsumeStatus s = VolumeAccumulator_Consume(&acc, &f2);
    if (s != VOLUME_OK) { FAIL("expected OK"); return; }

    const VolumeState *st = VolumeAccumulator_GetState(&acc);
    if (st->reverse_volume_ul != 1000) { FAIL("expected 1000 uL reverse"); return; }
    if (st->forward_volume_ul != 0) { FAIL("forward should be 0"); return; }
    PASS();
}

static void test_zero_flow_no_volume(void)
{
    VolumeAccumulator acc;
    VolumeAccumulator_Init(&acc, &test_config);

    FlowResult f1 = make_flow(0, 1000000, 1, FLOW_DIRECTION_NONE);
    assert(VolumeAccumulator_Consume(&acc, &f1) == VOLUME_ANCHORED);

    FlowResult f2 = make_flow(0, 2000000, 2, FLOW_DIRECTION_NONE);
    VolumeConsumeStatus s = VolumeAccumulator_Consume(&acc, &f2);
    if (s != VOLUME_OK) { FAIL("expected OK"); return; }

    const VolumeState *st = VolumeAccumulator_GetState(&acc);
    if (st->forward_volume_ul != 0) { FAIL("forward should be 0"); return; }
    if (st->reverse_volume_ul != 0) { FAIL("reverse should be 0"); return; }
    PASS();
}

static void test_direction_change(void)
{
    VolumeAccumulator acc;
    VolumeAccumulator_Init(&acc, &test_config);

    /* Forward interval */
    FlowResult f1 = make_flow(1000, 1000000, 1, FLOW_DIRECTION_FORWARD);
    assert(VolumeAccumulator_Consume(&acc, &f1) == VOLUME_ANCHORED);
    FlowResult f2 = make_flow(1000, 2000000, 2, FLOW_DIRECTION_FORWARD);
    assert(VolumeAccumulator_Consume(&acc, &f2) == VOLUME_OK);

    /* Reverse interval — anchor flow was +1000, so this interval adds to forward still */
    FlowResult f3 = make_flow(-500, 3000000, 3, FLOW_DIRECTION_REVERSE);
    assert(VolumeAccumulator_Consume(&acc, &f3) == VOLUME_OK);

    /* Now anchor is -500, so next interval adds to reverse */
    FlowResult f4 = make_flow(-500, 4000000, 4, FLOW_DIRECTION_REVERSE);
    assert(VolumeAccumulator_Consume(&acc, &f4) == VOLUME_OK);

    const VolumeState *st = VolumeAccumulator_GetState(&acc);
    /* f1→f2: 1000 uL/s × 1s = 1000 uL forward */
    /* f2→f3: 1000 uL/s × 1s = 1000 uL forward (anchor was +1000) */
    /* f3→f4: 500 uL/s × 1s = 500 uL reverse (anchor was -500 => abs=500) */
    if (st->forward_volume_ul != 2000) { FAIL("expected 2000 forward"); return; }
    if (st->reverse_volume_ul != 500)  { FAIL("expected 500 reverse"); return; }
    PASS();
}

static void test_equal_timestamp(void)
{
    VolumeAccumulator acc;
    VolumeAccumulator_Init(&acc, &test_config);

    FlowResult f1 = make_flow(1000, 1000000, 1, FLOW_DIRECTION_FORWARD);
    assert(VolumeAccumulator_Consume(&acc, &f1) == VOLUME_ANCHORED);

    /* Same timestamp, new identity → zero-interval */
    FlowResult f2 = make_flow(2000, 1000000, 2, FLOW_DIRECTION_FORWARD);
    VolumeConsumeStatus s = VolumeAccumulator_Consume(&acc, &f2);
    if (s != VOLUME_ZERO_INTERVAL) { FAIL("expected ZERO_INTERVAL"); return; }
    PASS();
}

static void test_large_gap_reanchors(void)
{
    VolumeAccumulator acc;
    VolumeAccumulator_Init(&acc, &test_config);

    FlowResult f1 = make_flow(1000, 1000000, 1, FLOW_DIRECTION_FORWARD);
    assert(VolumeAccumulator_Consume(&acc, &f1) == VOLUME_ANCHORED);

    /* Gap of 10 s > 5 s max → re-anchor */
    FlowResult f2 = make_flow(1000, 11000000, 2, FLOW_DIRECTION_FORWARD);
    VolumeConsumeStatus s = VolumeAccumulator_Consume(&acc, &f2);
    if (s != VOLUME_REJECTED_TIME) { FAIL("expected REJECTED_TIME (gap)"); return; }

    const VolumeState *st = VolumeAccumulator_GetState(&acc);
    if (st->forward_volume_ul != 0) { FAIL("forward should be 0 (no integration across gap)"); return; }
    PASS();
}

int main(void)
{
    printf("Volume Arithmetic Tests\n");
    printf("───────────────────────\n");
    test_first_sample_anchors();
    test_constant_forward_integration();
    test_reverse_integration();
    test_zero_flow_no_volume();
    test_direction_change();
    test_equal_timestamp();
    test_large_gap_reanchors();
    printf("───────────────────────\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
