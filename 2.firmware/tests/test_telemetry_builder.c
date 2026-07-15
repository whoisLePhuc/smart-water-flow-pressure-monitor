#include "services/telemetry_builder.h"
#include <stdio.h>
#include <string.h>

static int p=0,f=0;
#define T(n) printf("  %-45s ",n)
#define PASS() do{p++;printf("PASS\n");}while(0)
#define FAIL(m) do{f++;printf("FAIL: %s\n",m);}while(0)

static RuntimeSnapshot make_snapshot(void)
{
    RuntimeSnapshot s; memset(&s,0,sizeof(s));
    s.snapshot_version = 42;
    s.mode.mode_generation = 1;
    s.mode.current_mode = SYSTEM_MODE_NORMAL;
    s.flow.flow_ul_per_s = 1000;
    s.flow.direction = FLOW_DIRECTION_FORWARD;
    s.temperature.temperature_mdeg_c = 25000;
    s.pressure.pressure_pa = 300000;
    s.volume.forward_volume_ul = 5000000;
    s.volume.reverse_volume_ul = 100000;
    s.leak.state = LEAK_STATE_NORMAL;
    s.leak.evaluation_status = LEAK_EVAL_ACTIVE;
    s.active_config_version = 3;
    s.active_calibration_version = 2;
    return s;
}

static void test_build_record(void)
{
    TelemetryBuilder tb; TelemetryBuilder_Init(&tb);
    RuntimeSnapshot snap = make_snapshot();

    TelemetryRecord rec;
    bool ok = TelemetryBuilder_Build(&tb, &snap, 0, 1000000, 1, 5000000, 1000000, 2, 1, &rec);
    if (!ok) { FAIL("build failed"); return; }
    if (rec.record_sequence != 1) { FAIL("seq 1"); return; }
    if (rec.schema_version != TELEMETRY_SCHEMA_VERSION) { FAIL("schema"); return; }
    if (rec.flow_ul_per_s != 1000) { FAIL("flow"); return; }
    if (rec.forward_volume_ul != 5000000) { FAIL("volume"); return; }
    if (rec.leak_state != LEAK_STATE_NORMAL) { FAIL("leak state"); return; }
    PASS();
}

static void test_sequence_increments(void)
{
    TelemetryBuilder tb; TelemetryBuilder_Init(&tb);
    RuntimeSnapshot snap = make_snapshot();
    TelemetryRecord r1,r2;

    TelemetryBuilder_Build(&tb, &snap, 0, 100, 1, 0, 0, 0, 1, &r1);
    TelemetryBuilder_Build(&tb, &snap, 0, 200, 2, 0, 0, 0, 1, &r2);

    if (r2.record_sequence != r1.record_sequence + 1) { FAIL("seq increment"); return; }
    PASS();
}

static void test_invalid_fields_zero(void)
{
    TelemetryBuilder tb; TelemetryBuilder_Init(&tb);
    RuntimeSnapshot snap; memset(&snap,0,sizeof(snap));

    TelemetryRecord rec;
    TelemetryBuilder_Build(&tb, &snap, 0, 0, 0, 0, 0, 0, 0, &rec);

    /* All fields should be zero (not garbage) */
    if (rec.flow_ul_per_s != 0) { FAIL("flow should be 0"); return; }
    if (rec.forward_volume_ul != 0) { FAIL("vol 0"); return; }
    PASS();
}

int main(void)
{
    printf("Telemetry Builder Tests\n");
    printf("───────────────────────\n");
    test_build_record();
    test_sequence_increments();
    test_invalid_fields_zero();
    printf("───────────────────────\n");
    printf("%d passed, %d failed\n",p,f);
    return f>0;
}
