/**
 * test_volume_admission.c — Production admission gate tests (VOL-ADM-001)
 */
#include "services/volume_accumulator.h"
#include <stdio.h>
#include <string.h>

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

static FlowResult make_flow(MeasurementPurpose purpose, DataOrigin origin,
                             DataProvenance prov, DataValidity valid,
                             DataFreshness fresh, ProductionAcceptance accept)
{
    FlowResult f;
    memset(&f, 0, sizeof(f));
    f.meta.purpose    = purpose;
    f.meta.origin     = origin;
    f.meta.provenance = prov;
    f.meta.validity   = valid;
    f.meta.freshness  = fresh;
    f.meta.acceptance = accept;
    f.meta.source_generation = 1;
    f.meta.sample_sequence   = 1;
    f.meta.result_version    = 1;
    f.meta.sample_monotonic_us = 1000000;
    f.meta.binding.binding_id = 1;
    f.flow_ul_per_s = 1000;
    return f;
}

static void test_admission_accepts_valid_production(void)
{
    VolumeAccumulator acc;
    VolumeAccumulator_Init(&acc, &test_config);

    FlowResult f = make_flow(MEAS_PURPOSE_PRODUCTION, DATA_ORIGIN_LIVE_DEVICE,
                              PROVENANCE_MEASURED, DATA_VALID, DATA_FRESH, DATA_ACCEPTED);
    /* First sample anchors */
    VolumeConsumeStatus s1 = VolumeAccumulator_Consume(&acc, &f);
    if (s1 != VOLUME_ANCHORED) { FAIL("expected ANCHORED"); return; }

    /* Second sample integrates */
    f.meta.sample_sequence = 2;
    f.meta.sample_monotonic_us = 2000000;
    VolumeConsumeStatus s2 = VolumeAccumulator_Consume(&acc, &f);
    if (s2 != VOLUME_OK) { FAIL("expected OK"); return; }
    PASS();
}

static void test_admission_rejects_non_production(void)
{
    VolumeAccumulator acc;
    VolumeAccumulator_Init(&acc, &test_config);

    FlowResult f = make_flow(MEAS_PURPOSE_SERVICE, DATA_ORIGIN_LIVE_DEVICE,
                              PROVENANCE_MEASURED, DATA_VALID, DATA_FRESH, DATA_ACCEPTED);
    VolumeConsumeStatus s = VolumeAccumulator_Consume(&acc, &f);
    if (s != VOLUME_REJECTED_NON_PRODUCTION) { FAIL("expected REJECTED_NON_PRODUCTION"); return; }
    PASS();
}

static void test_admission_rejects_simulated(void)
{
    VolumeAccumulator acc;
    VolumeAccumulator_Init(&acc, &test_config);

    FlowResult f = make_flow(MEAS_PURPOSE_PRODUCTION, DATA_ORIGIN_SIMULATED_DEVICE,
                              PROVENANCE_MEASURED, DATA_VALID, DATA_FRESH, DATA_ACCEPTED);
    VolumeConsumeStatus s = VolumeAccumulator_Consume(&acc, &f);
    if (s != VOLUME_REJECTED_NON_PRODUCTION) { FAIL("expected REJECTED"); return; }
    PASS();
}

static void test_admission_rejects_estimated(void)
{
    VolumeAccumulator acc;
    VolumeAccumulator_Init(&acc, &test_config);

    FlowResult f = make_flow(MEAS_PURPOSE_PRODUCTION, DATA_ORIGIN_LIVE_DEVICE,
                              PROVENANCE_ESTIMATED, DATA_VALID, DATA_FRESH, DATA_ACCEPTED);
    VolumeConsumeStatus s = VolumeAccumulator_Consume(&acc, &f);
    if (s != VOLUME_REJECTED_NON_PRODUCTION) { FAIL("expected REJECTED"); return; }
    PASS();
}

static void test_admission_rejects_invalid(void)
{
    VolumeAccumulator acc;
    VolumeAccumulator_Init(&acc, &test_config);

    FlowResult f = make_flow(MEAS_PURPOSE_PRODUCTION, DATA_ORIGIN_LIVE_DEVICE,
                              PROVENANCE_MEASURED, DATA_INVALID, DATA_FRESH, DATA_ACCEPTED);
    VolumeConsumeStatus s = VolumeAccumulator_Consume(&acc, &f);
    if (s != VOLUME_REJECTED_INVALID) { FAIL("expected REJECTED_INVALID"); return; }
    PASS();
}

static void test_admission_rejects_stale(void)
{
    VolumeAccumulator acc;
    VolumeAccumulator_Init(&acc, &test_config);

    FlowResult f = make_flow(MEAS_PURPOSE_PRODUCTION, DATA_ORIGIN_LIVE_DEVICE,
                              PROVENANCE_MEASURED, DATA_VALID, DATA_STALE, DATA_ACCEPTED);
    VolumeConsumeStatus s = VolumeAccumulator_Consume(&acc, &f);
    if (s != VOLUME_REJECTED_STALE) { FAIL("expected REJECTED_STALE"); return; }
    PASS();
}

int main(void)
{
    printf("Volume Admission Tests\n");
    printf("──────────────────────\n");
    test_admission_accepts_valid_production();
    test_admission_rejects_non_production();
    test_admission_rejects_simulated();
    test_admission_rejects_estimated();
    test_admission_rejects_invalid();
    test_admission_rejects_stale();
    printf("──────────────────────\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
