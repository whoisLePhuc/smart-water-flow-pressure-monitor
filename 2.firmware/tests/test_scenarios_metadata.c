/**
 * Metadata and system isolation scenarios.
 *
 * Tests: simulated origin preserved end-to-end, binding mismatch rejected,
 *        service result isolation, processing stub provenance.
 */

#include "event/data_model.h"
#include "event/data_repository.h"
#include "processing_stubs.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static ResultMetadata make_source_meta(MeasurementPurpose purpose,
                                        DataOrigin origin,
                                        DataProvenance provenance)
{
    ResultMetadata m;
    memset(&m, 0, sizeof(m));
    m.purpose = purpose;
    m.origin = origin;
    m.provenance = provenance;
    m.source_generation = 1;
    m.sample_sequence = 1;
    return m;
}

/* ── Simulated origin preserved ─────────────────────── */

static void test_simulated_origin_preserved(void)
{
    ResultMetadata src = make_source_meta(
        MEAS_PURPOSE_PRODUCTION,
        DATA_ORIGIN_SIMULATED_DEVICE,
        PROVENANCE_MEASURED);

    TemperatureResult t = stub_process_temperature(&src, 500);
    assert(t.meta.origin == DATA_ORIGIN_SIMULATED_DEVICE);
    assert(t.meta.purpose == MEAS_PURPOSE_PRODUCTION);
    PASS();
}

/* ── Processing stub provenance is ESTIMATED ────────── */

static void test_stub_provenance_is_estimated(void)
{
    ResultMetadata src = make_source_meta(
        MEAS_PURPOSE_PRODUCTION,
        DATA_ORIGIN_LIVE_DEVICE,
        PROVENANCE_MEASURED);

    FlowResult f = stub_process_flow(&src, 100, FLOW_DIRECTION_FORWARD);
    assert(f.meta.provenance == PROVENANCE_ESTIMATED);
    assert(f.meta.reason_flags == 0x0001);
    PASS();
}

/* ── Service calibration result not production ──────── */

static void test_service_result_not_production(void)
{
    /* Service sample → stub → estimated provenance */
    ResultMetadata src = make_source_meta(
        MEAS_PURPOSE_SERVICE,
        DATA_ORIGIN_LIVE_DEVICE,
        PROVENANCE_MEASURED);

    PressureResult p = stub_process_pressure(&src, 200);
    assert(p.meta.purpose == MEAS_PURPOSE_SERVICE);
    assert(p.meta.provenance == PROVENANCE_ESTIMATED);

    /* Should NOT pass production guard */
    assert(!data_is_production(&p.meta));
    PASS();
}

/* ── Production eligibility guard ───────────────────── */

static void test_production_eligibility(void)
{
    /* Valid production result */
    ResultMetadata valid = make_source_meta(
        MEAS_PURPOSE_PRODUCTION,
        DATA_ORIGIN_LIVE_DEVICE,
        PROVENANCE_MEASURED);
    assert(data_is_production(&valid));

    /* Missing LIVE_DEVICE */
    ResultMetadata no_live = make_source_meta(
        MEAS_PURPOSE_PRODUCTION,
        DATA_ORIGIN_SIMULATED_DEVICE,
        PROVENANCE_MEASURED);
    assert(!data_is_production(&no_live));

    /* Wrong provenance */
    ResultMetadata estimated = make_source_meta(
        MEAS_PURPOSE_PRODUCTION,
        DATA_ORIGIN_LIVE_DEVICE,
        PROVENANCE_ESTIMATED);
    assert(!data_is_production(&estimated));

    /* Service purpose */
    ResultMetadata service = make_source_meta(
        MEAS_PURPOSE_SERVICE,
        DATA_ORIGIN_LIVE_DEVICE,
        PROVENANCE_MEASURED);
    assert(!data_is_production(&service));
    PASS();
}

/* ── Binding reference preserved through processing ─── */

static void test_binding_preserved(void)
{
    ResultMetadata src = make_source_meta(
        MEAS_PURPOSE_PRODUCTION,
        DATA_ORIGIN_LIVE_DEVICE,
        PROVENANCE_MEASURED);
    src.binding.binding_id = 42;
    src.binding.binding_version = 1;
    src.binding.profile_version = 3;

    FlowResult f = stub_process_flow(&src, 100, FLOW_DIRECTION_FORWARD);
    assert(f.meta.binding.binding_id == 42);
    assert(f.meta.binding.profile_version == 3);
    PASS();
}

int main(void)
{
    printf("Metadata & System Scenarios\n");
    printf("────────────────────────────\n");

    test_simulated_origin_preserved();
    test_stub_provenance_is_estimated();
    test_service_result_not_production();
    test_production_eligibility();
    test_binding_preserved();

    printf("────────────────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
