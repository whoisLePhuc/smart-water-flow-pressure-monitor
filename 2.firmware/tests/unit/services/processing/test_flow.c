/**
 * Flow measurement processing tests
 */

#include "services/processing/flow_service.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int tests_passed = 0, tests_failed = 0;
#define TEST(name) do { printf("  %-40s ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static const FlowProfile test_profile = {
    .id = { .profile_id = 1, .schema_version = 1, .qualification_status = 1 },
    .pipe_area = 1000,
    .path_length = 100000,
    .acoustic_velocity = 1480000,
};
static const CalibrationRecord test_cal = {
    .record_version = 1, .gain = 1024, .offset = 0, .shift = 10,
};

static void test_flow_forward(void)
{
    FlowCandidate c;
    FlowProcessStatus st = flow_compute(100000000, 100001000, 25000, &test_profile, &test_cal, &c);
    assert(st == FLOW_OK);
    assert(c.direction == FLOW_DIRECTION_FORWARD);
    PASS();
}

static void test_flow_reverse(void)
{
    FlowCandidate c;
    FlowProcessStatus st = flow_compute(100001000, 100000000, 25000, &test_profile, &test_cal, &c);
    assert(st == FLOW_OK);
    assert(c.direction == FLOW_DIRECTION_REVERSE);
    PASS();
}

static void test_flow_null_profile(void)
{
    FlowCandidate c;
    FlowProcessStatus st = flow_compute(100000000, 100001000, 25000, NULL, &test_cal, &c);
    assert(st == FLOW_INTERNAL_ERROR);
    PASS();
}

int main(void)
{
    printf("Flow Processing Tests\n");
    printf("─────────────────────\n");
    test_flow_forward();
    test_flow_reverse();
    test_flow_null_profile();
    printf("─────────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
