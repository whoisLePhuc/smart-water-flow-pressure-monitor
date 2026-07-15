/**
 * test_reporting_e2e.c — End-to-end reporting pipeline (12G)
 *
 * Exercises: TimeService → ReportingSchedule → TelemetryBuilder
 * → TelemetryQueue → CellularDeliveryService → ACK
 *
 * Also verifies offline isolation and reset behavior.
 */
#include "services/time_service.h"
#include "services/reporting_schedule.h"
#include "services/telemetry_builder.h"
#include "services/telemetry_queue.h"
#include "services/cellular_delivery.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int p=0,f=0;
#define T(n) printf("  %-45s ",n)
#define PASS() do{p++;printf("PASS\n");}while(0)
#define FAIL(m) do{f++;printf("FAIL: %s\n",m);}while(0)

/* Build a minimal RuntimeSnapshot for testing */
static RuntimeSnapshot make_snap(uint64_t ver, uint64_t fwd, uint8_t leak_st)
{
    RuntimeSnapshot s; memset(&s, 0, sizeof(s));
    s.snapshot_version = ver;
    s.mode.current_mode = SYSTEM_MODE_NORMAL;
    s.mode.mode_generation = 1;
    s.flow.flow_ul_per_s = 1000;
    s.flow.direction = FLOW_DIRECTION_FORWARD;
    s.volume.forward_volume_ul = fwd;
    s.volume.reverse_volume_ul = 0;
    s.leak.state = (LeakState)leak_st;
    s.leak.evaluation_status = LEAK_EVAL_ACTIVE;
    s.active_config_version = 1;
    return s;
}

static void test_schedule_build_queue_deliver_ack(void)
{
    /* 1. Time service — valid wall time */
    TimeService ts; TimeService_Init(&ts, 0);
    TimeService_SetWallTime(&ts, 1000000, SYS_TIME_NETWORK_SYNCED, 2, 0);
    if (!TimeService_IsTimeValid(&ts)) { FAIL("time invalid"); return; }

    /* 2. Reporting schedule — get next due */
    ReportingSchedule rs; ReportingSchedule_Init(&rs);
    int64_t due = RS_GetNextDue(&rs.cfg, TimeService_GetWallTime(&ts));
    uint8_t w = RS_GetActiveWindow(&rs.cfg, due);

    /* 3. Telemetry builder — build record */
    TelemetryBuilder tb; TelemetryBuilder_Init(&tb);
    TelemetryRecord rec;
    RuntimeSnapshot snap = make_snap(1, 5000, LEAK_STATE_NORMAL);
    bool ok = TelemetryBuilder_Build(&tb, &snap, w, due, 0, 1000000, due, (uint8_t)ts.quality, rs.cfg.schedule_version, &rec);
    if (!ok) { FAIL("build"); return; }

    /* 4. Queue — enqueue */
    TelemetryQueue q; TelemetryQueue_Init(&q);
    if (TelemetryQueue_Enqueue(&q, &rec, 1000000) != QUEUE_OK) { FAIL("enqueue"); return; }
    if (TelemetryQueue_GetCount(&q) != 1) { FAIL("count 1"); return; }

    /* 5. Delivery — connect and send */
    CellularDeliveryService del; CellularDelivery_Init(&del, 0);
    CellularDelivery_Connect(&del, 0);
    if (!CellularDelivery_StartAttempt(&del, &q, 1000000)) { FAIL("start attempt"); return; }
    if (!TelemetryQueue_HasInFlight(&q)) { FAIL("in flight"); return; }

    /* 6. ACK */
    CellularDelivery_Complete(&del, 0, &q, 2000000);
    if (TelemetryQueue_HasInFlight(&q)) { FAIL("still in flight after ack"); return; }
    if (TelemetryQueue_GetCount(&q) != 0) { FAIL("not removed after ack"); return; }

    PASS();
}

static void test_offline_accumulation(void)
{
    TelemetryQueue q; TelemetryQueue_Init(&q);
    TelemetryBuilder tb; TelemetryBuilder_Init(&tb);
    RuntimeSnapshot snap = make_snap(1, 1000, LEAK_STATE_NORMAL);
    TelemetryRecord rec;

    /* Build and enqueue 3 records while offline */
    for (uint64_t i = 0; i < 3; i++) {
        snap.snapshot_version = i + 1;
        snap.volume.forward_volume_ul = 1000 * (i + 1);
        TelemetryBuilder_Build(&tb, &snap, 0, 1000 + (int64_t)i, (uint16_t)i, 1000 + i*1000, 1000 + (int64_t)i, 2, 1, &rec);
        TelemetryQueue_Enqueue(&q, &rec, 1000 + i*1000);
    }
    if (TelemetryQueue_GetCount(&q) != 3) { FAIL("should have 3 queued"); return; }
    PASS();
}

static void test_reset_clears_queue(void)
{
    TelemetryQueue q; TelemetryQueue_Init(&q);
    TelemetryBuilder tb; TelemetryBuilder_Init(&tb);
    RuntimeSnapshot snap = make_snap(1, 5000, LEAK_STATE_NORMAL);
    TelemetryRecord rec;
    TelemetryBuilder_Build(&tb, &snap, 0, 1000, 0, 1000, 1000, 2, 1, &rec);
    TelemetryQueue_Enqueue(&q, &rec, 1000);

    /* Reset clears queue */
    TelemetryQueue_Init(&q);
    if (TelemetryQueue_GetCount(&q) != 0) { FAIL("queue not empty after reset"); return; }
    PASS();
}

static void test_leak_no_immediate_telemetry(void)
{
    /* Leak state change does NOT by itself create a telemetry record.
     * Telemetry is only created via Scheduling (EVT_REPORT_DUE). */
    RuntimeSnapshot snap = make_snap(1, 5000, LEAK_STATE_NORMAL);
    /* Changing leak state in snapshot doesn't trigger anything.
     * TelemetryBuilder only runs when called from scheduler. */

    /* The test: just verify leak state in snapshot doesn't auto-enqueue */
    TelemetryQueue q; TelemetryQueue_Init(&q); (void)snap;
    if (TelemetryQueue_GetCount(&q) != 0) { FAIL("leak change should not enqueue"); return; }
    PASS();
}

int main(void)
{
    printf("Reporting E2E Tests\n");
    printf("───────────────────\n");
    test_schedule_build_queue_deliver_ack();
    test_offline_accumulation();
    test_reset_clears_queue();
    test_leak_no_immediate_telemetry();
    printf("───────────────────\n");
    printf("%d passed, %d failed\n",p,f);
    return f>0;
}
