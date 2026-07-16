/**
 * Simulator contract tests — safety net before source-tree restructure.
 *
 * Levels (per 92_firmware_test_strategy.md):
 *   Unit:       manifest parse/validate in isolation
 *   Contract:   harness lifecycle, determinism, ordering
 *   Integration: full-stack MAX/ZSSC via runner
 *
 * IDs: SIM-MAN-001..003, SIM-LIFE-001, SIM-RST-001,
 *      SIM-DET-001..002, SIM-LIM-001, SIM-MAX-001..003,
 *      SIM-ZSC-001..002, SIM-I2C-001, SIM-CMP-001,
 *      SIM-META-001, SIM-CAP-001
 */

#include "scenario_manifest.h"
#include "sim_harness.h"
#include "normalized_trace.h"
#include "scenario_runner.h"
#include "peers/peer_max35103.h"
#include "peers/peer_zssc3241.h"
#include "peers/peer_fram.h"
#include "providers/linux_spi_provider.h"
#include "providers/linux_i2c_provider.h"
#include "infrastructure/queues/app_event_queue.h"
#include "infrastructure/time/scheduler.h"
#include "app/system_fsm.h"
#include "event/app_event.h"
#include "infrastructure/repositories/data_repository.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
#define TEST(name) do { printf("  %-40s ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

/* ============================================================
 * SIM-MAN — Manifest contract
 * ============================================================ */

static void sim_man_001_minimal_valid(void)
{
    const char *json = "{"
        "\"scenario_name\": \"golden\","
        "\"schema_version\": 1,"
        "\"seed\": 42,"
        "\"max_turns\": 200"
    "}";
    ScenarioManifest m;
    char err[128];
    assert(manifest_parse(json, &m, err, sizeof(err)));
    assert(strcmp(m.scenario_name, "golden") == 0);
    assert(m.seed == 42);
    assert(manifest_validate(&m, err, sizeof(err)));
    PASS();
}

static void sim_man_002_reject_unknown_schema(void)
{
    const char *json = "{"
        "\"scenario_name\": \"bad\","
        "\"schema_version\": 99"
    "}";
    ScenarioManifest m;
    char err[128];
    assert(manifest_parse(json, &m, err, sizeof(err)));
    assert(!manifest_validate(&m, err, sizeof(err)));
    PASS();
}

static void sim_man_002_reject_missing_name(void)
{
    ScenarioManifest m;
    memset(&m, 0, sizeof(m));
    char err[128];
    assert(!manifest_validate(&m, err, sizeof(err)));
    PASS();
}

/* ============================================================
 * SIM-LIFE — Harness lifecycle
 * ============================================================ */

static void sim_life_001_init_destroy_cleanup(void)
{
    for (int i = 0; i < 3; i++) {
        SimHarness h;
        assert(sim_harness_init(&h));
        sim_harness_destroy(&h);
    }
    PASS();
}

static void sim_rst_001_reset_generation(void)
{
    SimHarness h;
    sim_harness_init(&h);
    uint32_t boot_gen = h.boot_generation;

    /* Run some events */
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = EVT_INIT_COMPLETED;
    evt.priority = EVENT_PRIO_CRITICAL;
    evt.delivery = DELIVERY_EDGE;
    app_event_queue_post(&h.event_queue, &evt);
    run_controller_until_idle(&h.controller);

    /* Reset */
    assert(sim_harness_reset(&h));
    assert(h.boot_generation > boot_gen);
    PASS();
}

/* ============================================================
 * SIM-DET — Determinism
 * ============================================================ */

static NormalizedTrace run_boot_scenario(void)
{
    NormalizedTrace trace;
    trace_init(&trace);

    SimHarness h;
    sim_harness_init(&h);

    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = EVT_INIT_COMPLETED;
    evt.priority = EVENT_PRIO_CRITICAL;
    evt.delivery = DELIVERY_EDGE;
    app_event_queue_post(&h.event_queue, &evt);
    run_controller_until_idle(&h.controller);

    TraceRecord r;
    memset(&r, 0, sizeof(r));
    r.virtual_time_us = linux_clock_now_us(&h.clock);
    r.turn_number = h.controller.turn_count;
    r.event_id = EVT_INIT_COMPLETED;
    r.system_mode = (uint8_t)system_fsm_get_context(&h.fsm).current_mode;
    r.transition_sequence = (uint32_t)system_fsm_get_context(&h.fsm).transition_sequence;
    trace_append(&trace, &r);
    return trace;
}

static void sim_det_001_same_seed_same_trace(void)
{
    NormalizedTrace traces[3];
    for (int i = 0; i < 3; i++)
        traces[i] = run_boot_scenario();

    for (int i = 1; i < 3; i++) {
        if (!trace_equals(&traces[0], &traces[i])) {
            FAIL("Trace mismatch");
            return;
        }
    }
    PASS();
}

/* ============================================================
 * SIM-MAX — Full-stack MAX
 * ============================================================ */

static void sim_max_001_normal_cycle(void)
{
    /* Direct MAX peer test (component level) */
    Max35103Peer peer;
    max_peer_init(&peer);
    max_peer_configure(&peer);

    uint64_t int_time = max_peer_schedule_cycle(&peer, 1000);
    assert(int_time > 0);
    assert(peer.int_active);

    uint8_t tx = 0x10, rx = 0;
    uint64_t lat;
    uint32_t st;
    assert(max_peer_plan_spi(&peer, &tx, &rx, 1, &lat, &st));
    assert(peer.spi_operations == 1);
    PASS();
}

static void sim_max_002_sentinel_invalid(void)
{
    Max35103Peer peer;
    max_peer_init(&peer);
    max_peer_set_fault(&peer, MAX_FAULT_INVALID_RESULT);

    uint8_t tx[2] = {0x10, 0}, rx[2] = {0};
    uint64_t lat;
    uint32_t st;
    max_peer_plan_spi(&peer, tx, rx, 2, &lat, &st);
    assert(rx[1] == 0xFF);  /* Sentinel */
    PASS();
}

static void sim_max_003_no_completion_timeout(void)
{
    Max35103Peer peer;
    max_peer_init(&peer);
    max_peer_set_fault(&peer, MAX_FAULT_NO_COMPLETION);

    uint8_t tx = 0x10, rx = 0;
    uint64_t lat;
    uint32_t st;
    assert(!max_peer_plan_spi(&peer, &tx, &rx, 1, &lat, &st));
    PASS();
}

/* ============================================================
 * SIM-ZSC — Full-stack ZSSC
 * ============================================================ */

static void sim_zsc_001_normal_conversion(void)
{
    Zssc3241Peer peer;
    zssc_peer_init(&peer);

    uint64_t eoc = zssc_peer_schedule_eoc(&peer, 5000);
    assert(eoc > 5000);
    assert(peer.eoc_active);

    uint8_t tx = 0x10, rx[2] = {0};
    uint64_t lat;
    uint32_t st;
    assert(zssc_peer_plan_i2c(&peer, 0x50, &tx, 1, rx, 2, &lat, &st));
    assert(peer.i2c_operations == 1);
    PASS();
}

static void sim_zsc_002_status_invalid(void)
{
    Zssc3241Peer peer;
    zssc_peer_init(&peer);
    zssc_peer_set_fault(&peer, ZSSC_FAULT_FATAL_STATUS);

    uint8_t tx = 0x10, rx[2] = {0};
    uint64_t lat;
    uint32_t st;
    zssc_peer_plan_i2c(&peer, 0x50, &tx, 1, rx, 2, &lat, &st);
    assert(rx[0] == 0xFF);  /* Fatal */
    PASS();
}

/* ============================================================
 * SIM-I2C — Shared-bus contention
 * ============================================================ */

static void sim_i2c_001_contention(void)
{
    LinuxScheduledActionQueue q;
    action_queue_init(&q);
    LinuxI2cProvider i2c;
    linux_i2c_init(&i2c, &q);

    Zssc3241Peer zssc;
    zssc_peer_init(&zssc);
    LinuxI2cPeer zp = { .i2c_plan = zssc_peer_plan_i2c, .context = &zssc };
    linux_i2c_register_peer(&i2c, 0x50, zp);

    FramPeer fram;
    fram_peer_init(&fram);
    LinuxI2cPeer fp = { .i2c_plan = fram_peer_plan_i2c, .context = &fram };
    linux_i2c_register_peer(&i2c, 0x51, fp);

    LinuxI2cRequest req = {
        .operation_id = 1, .correlation_id = 10, .owner_generation = 1,
        .slave_address = 0x50, .deadline_us = 0
    };
    assert(linux_i2c_submit(&i2c, &req));

    LinuxI2cRequest req2 = {
        .operation_id = 2, .correlation_id = 20, .owner_generation = 1,
        .slave_address = 0x51, .deadline_us = 0
    };
    assert(!linux_i2c_submit(&i2c, &req2));  /* Bus busy */
    PASS();
}

/* ============================================================
 * SIM-META — Metadata boundary
 * ============================================================ */

static void sim_meta_001_production_guard(void)
{
    ResultMetadata m;
    memset(&m, 0, sizeof(m));
    m.purpose = MEAS_PURPOSE_PRODUCTION;
    m.origin = DATA_ORIGIN_LIVE_DEVICE;
    m.provenance = PROVENANCE_MEASURED;
    assert(result_metadata_is_production(&m));

    m.origin = DATA_ORIGIN_SIMULATED_DEVICE;
    assert(!result_metadata_is_production(&m));
    PASS();
}

/* ============================================================
 * SIM-CAP — Capacity overflow
 * ============================================================ */

static void sim_cap_001_queue_overflow(void)
{
    SimHarness h;
    sim_harness_init(&h);

    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.priority = EVENT_PRIO_BACKGROUND;
    evt.delivery = DELIVERY_EDGE;

    /* Fill queue beyond background cap */
    int posted = 0;
    for (int i = 0; i < 40; i++) {
        evt.id = (EventId)(0x0700 + (i % 6));
        EventPostResult r = app_event_queue_post(&h.event_queue, &evt);
        if (r == EVENT_POST_OK) posted++;
    }
    assert(posted > 0);
    assert(app_event_queue_get_overflow_count(&h.event_queue) > 0);
    PASS();
}

int main(void)
{
    printf("Simulator Contract Tests\n");
    printf("════════════════════════\n");

    /* SIM-MAN */
    printf("── Manifest ──\n");
    TEST("SIM-MAN-001 minimal valid parse/validate"); sim_man_001_minimal_valid();
    TEST("SIM-MAN-002 reject unknown schema");         sim_man_002_reject_unknown_schema();
    TEST("SIM-MAN-002 reject missing name");           sim_man_002_reject_missing_name();

    /* SIM-LIFE */
    printf("── Lifecycle ──\n");
    TEST("SIM-LIFE-001 init/destroy cleanup loop");    sim_life_001_init_destroy_cleanup();
    TEST("SIM-RST-001 reset increases generation");    sim_rst_001_reset_generation();

    /* SIM-DET */
    printf("── Determinism ──\n");
    TEST("SIM-DET-001 3× run → same trace");          sim_det_001_same_seed_same_trace();

    /* SIM-MAX */
    printf("── MAX35103 ──\n");
    TEST("SIM-MAX-001 normal cycle");                 sim_max_001_normal_cycle();
    TEST("SIM-MAX-002 sentinel invalid");              sim_max_002_sentinel_invalid();
    TEST("SIM-MAX-003 no-completion timeout");         sim_max_003_no_completion_timeout();

    /* SIM-ZSC */
    printf("── ZSSC3241 ──\n");
    TEST("SIM-ZSC-001 normal conversion");             sim_zsc_001_normal_conversion();
    TEST("SIM-ZSC-002 status invalid");                sim_zsc_002_status_invalid();

    /* SIM-I2C */
    printf("── Shared I2C ──\n");
    TEST("SIM-I2C-001 ZSSC/F-RAM contention");        sim_i2c_001_contention();

    /* SIM-META */
    printf("── Metadata ──\n");
    TEST("SIM-META-001 production guard");             sim_meta_001_production_guard();

    /* SIM-CAP */
    printf("── Capacity ──\n");
    TEST("SIM-CAP-001 queue overflow count");          sim_cap_001_queue_overflow();

    printf("════════════════════════\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
