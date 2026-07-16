/**
 * Simulation harness & scenario runner integration tests.
 */

#include "sim_harness.h"
#include "scenario_manifest.h"
#include "normalized_trace.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static void test_harness_init_destroy(void)
{
    SimHarness h;
    bool ok = sim_harness_init(&h);
    assert(ok);
    assert(h.initialized);
    assert(h.boot_generation == 1);

    sim_harness_destroy(&h);
    assert(!h.initialized);
    PASS();
}

static void test_trace_append_and_equals(void)
{
    NormalizedTrace t1, t2;
    trace_init(&t1);
    trace_init(&t2);

    TraceRecord r = { .virtual_time_us = 100, .turn_number = 1, .event_id = 0x0101 };
    assert(trace_append(&t1, &r));
    assert(trace_append(&t2, &r));

    assert(trace_equals(&t1, &t2));

    /* Different traces should not match */
    TraceRecord r2 = { .virtual_time_us = 200, .turn_number = 2, .event_id = 0x0102 };
    assert(trace_append(&t1, &r2));
    assert(!trace_equals(&t1, &t2));
    PASS();
}

static void test_manifest_parse_basic(void)
{
    const char *json = "{"
        "\"scenario_name\": \"test_boot\","
        "\"schema_version\": 1,"
        "\"max_turns\": 100"
    "}";

    ScenarioManifest m;
    char err[128];
    bool ok = manifest_parse(json, &m, err, sizeof(err));
    assert(ok);
    assert(strcmp(m.scenario_name, "test_boot") == 0);
    assert(m.schema_version == 1);
    assert(m.max_turns == 100);
    PASS();
}

static void test_manifest_validate(void)
{
    ScenarioManifest m;
    memset(&m, 0, sizeof(m));
    strcpy(m.scenario_name, "test");
    m.schema_version = 1;

    char err[128];
    assert(manifest_validate(&m, err, sizeof(err)));
    PASS();
}

static void test_manifest_validate_missing_name(void)
{
    ScenarioManifest m;
    memset(&m, 0, sizeof(m));
    m.schema_version = 1;

    char err[128];
    assert(!manifest_validate(&m, err, sizeof(err)));
    PASS();
}

int main(void)
{
    printf("Simulation Harness Tests\n");
    printf("─────────────────────────\n");

    test_harness_init_destroy();
    test_trace_append_and_equals();
    test_manifest_parse_basic();
    test_manifest_validate();
    test_manifest_validate_missing_name();

    printf("─────────────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
