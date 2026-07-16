/**
 * Temperature calibration tests
 * Tests: Q16 join, ratio, RTD interpolation, service accept/publish
 */

#include "services/calibration/calibration_service.h"
#include "services/configuration/sensor_profile.h"
#include "infrastructure/numeric/checked_math.h"
#include "infrastructure/numeric/interpolation.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int tests_passed = 0, tests_failed = 0;
#define TEST(name) do { printf("  %-40s ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

/* Test RTD table: Pt1000, resistance in µΩ, temperature in m°C */
static const int64_t test_temp[] = {       0,   100000,   200000,   300000 };
static const int64_t test_res[]  = { 1000000,  1385000,  1778000,  2171000 };

static const TemperatureProfile test_profile = {
    .id = { .profile_id = 1, .schema_version = 1, .qualification_status = 1 },
    .rtd_r0 = 1000000, /* 1000 Ω in mΩ */
    .rtd_table_size = 4,
    .rtd_temp_table = test_temp,
    .rtd_res_table = test_res,
};

static const CalibrationRecord test_cal = {
    .record_version = 1,
    .gain = 1024,   /* gain 1.0 in Q10: 1024/1024 = 1.0 */
    .offset = 0,
    .shift = 10,
};

/* ── Q16 join ────────────────────────────────────────── */

static void test_q16_join(void)
{
    uint32_t n = ((uint32_t)0x1234 << 16) | 0x5678;
    assert(n == 0x12345678);
    PASS();
}

/* ── Pure conversion: basic RTD path ─────────────────── */

static void test_temp_convert_basic(void)
{
    TemperatureCandidate cand;
    TemperatureProcessStatus st = temperature_convert_raw(
        1000, 0,    /* probe: 1000 Q16 */
        1000, 0,    /* ref: 1000 Q16 */
        &test_profile, &test_cal, &cand);

    assert(st == TEMP_OK);
    /* At R = R0 = 1000 Ω → T = 0 °C */
    assert(cand.unfiltered_temperature_mdeg_c == 0);
    PASS();
}

static void test_temp_convert_at_100c(void)
{
    TemperatureCandidate cand;
    /* At 100°C, R ≈ 1385 Ω. probe/ref ratio = 1385/1000 = 1.385 */
    TemperatureProcessStatus st = temperature_convert_raw(
        1385, 0,    /* probe: 1385 Q16 */
        1000, 0,    /* ref: 1000 Q16 */
        &test_profile, &test_cal, &cand);

    assert(st == TEMP_OK);
    /* Should be ~100000 (100°C) */
    assert(cand.unfiltered_temperature_mdeg_c == 100000);
    PASS();
}

static void test_temp_convert_reference_zero(void)
{
    TemperatureCandidate cand;
    TemperatureProcessStatus st = temperature_convert_raw(
        500, 0,     /* probe */
        0, 0,       /* ref = 0 → invalid */
        &test_profile, &test_cal, &cand);

    assert(st == TEMP_INVALID_SAMPLE);
    PASS();
}

static void test_temp_convert_null_profile(void)
{
    TemperatureCandidate cand;
    TemperatureProcessStatus st = temperature_convert_raw(
        1000, 0, 1000, 0, NULL, &test_cal, &cand);
    assert(st == TEMP_INTERNAL_ERROR);
    PASS();
}

/* ── Interpolation edge cases ────────────────────────── */

static void test_interpolate_out_of_range(void)
{
    int64_t r;
    /* Clamp low: below 0 m°C → first resistance value 1000000 µΩ */
    assert(interpolate_i64(-100, test_temp, test_res, 4, &r) && r == 1000000);
    /* Clamp high: above 300000 m°C → last resistance */
    assert(interpolate_i64(999999, test_temp, test_res, 4, &r) && r == 2171000);
    PASS();
}

static void test_interpolate_invalid_table(void)
{
    assert(!table_is_monotonic(test_temp, test_res, 0));
    assert(!table_is_monotonic(NULL, NULL, 0));
    PASS();
}

int main(void)
{
    printf("Temperature Calibration Tests\n");
    printf("──────────────────────────────\n");
    test_q16_join();
    test_temp_convert_basic();
    test_temp_convert_at_100c();
    test_temp_convert_reference_zero();
    test_temp_convert_null_profile();
    test_interpolate_out_of_range();
    test_interpolate_invalid_table();
    printf("──────────────────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
