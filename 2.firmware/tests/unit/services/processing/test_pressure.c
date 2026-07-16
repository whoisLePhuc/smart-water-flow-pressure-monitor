/**
 * Pressure measurement processing tests
 */
#include "services/processing/pressure_service.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int passed = 0, failed = 0;
#define TEST(n) do { printf("  %-40s ", n); } while(0)
#define PASS() do { printf("PASS\n"); passed++; } while(0)
#define FAIL(m) do { printf("FAIL: %s\n", m); failed++; } while(0)

static const PressureProfile profile = {
    .id = { .profile_id = 1, .schema_version = 1, .qualification_status = 1 },
    .pa_min = 0, .pa_max = 1000000,
    .endpoint_lo_raw = 0, .endpoint_hi_raw = 0xFFFFFF,
    .endpoint_lo_pa = 0, .endpoint_hi_pa = 1000000,
};
static const CalibrationRecord cal = { .record_version = 1, .gain = 1024, .offset = 0, .shift = 10 };

static void test_pressure_midpoint(void)
{
    PressureCandidate c;
    PressureProcessStatus st = pressure_convert(0x7FFFFF, 0x40, &profile, &cal, &c);
    assert(st == PRESSURE_OK);
    assert(c.pressure_pa > 400000 && c.pressure_pa < 600000);
    PASS();
}

static void test_pressure_invalid_status(void)
{
    PressureCandidate c;
    assert(pressure_convert(1000, 0xFF, &profile, &cal, &c) == PRESSURE_STATUS_INVALID);
    assert(pressure_convert(1000, 0x00, &profile, &cal, &c) == PRESSURE_STATUS_INVALID);
    PASS();
}

int main(void)
{
    printf("Pressure Processing Tests\n");
    printf("─────────────────────────\n");
    test_pressure_midpoint();
    test_pressure_invalid_status();
    printf("─────────────────────────\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
