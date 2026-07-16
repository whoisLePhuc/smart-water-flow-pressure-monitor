#include "services/connectivity/telemetry_queue.h"
#include <stdio.h>
#include <string.h>

static int p=0,f=0;
#define T(n) printf("  %-45s ",n)
#define PASS() do{p++;printf("PASS\n");}while(0)
#define FAIL(m) do{f++;printf("FAIL: %s\n",m);}while(0)

static TelemetryRecord make_rec(uint64_t seq)
{
    TelemetryRecord r; memset(&r,0,sizeof(r)); r.record_sequence = seq; return r;
}

static void test_init_empty(void)
{
    TelemetryQueue q; TelemetryQueue_Init(&q);
    if (TelemetryQueue_GetCount(&q) != 0) { FAIL("not empty"); return; }
    PASS();
}

static void test_enqueue_dequeue(void)
{
    TelemetryQueue q; TelemetryQueue_Init(&q);
    TelemetryRecord r = make_rec(1);
    if (TelemetryQueue_Enqueue(&q, &r, 1000) != QUEUE_OK) { FAIL("enq"); return; }
    if (TelemetryQueue_GetCount(&q) != 1) { FAIL("count 1"); return; }

    TelemetryRecord out;
    if (!TelemetryQueue_Dequeue(&q, &out)) { FAIL("deq"); return; }
    if (out.record_sequence != 1) { FAIL("seq 1"); return; }
    PASS();
}

static void test_ack_removes(void)
{
    TelemetryQueue q; TelemetryQueue_Init(&q);
    TelemetryRecord r = make_rec(1);
    TelemetryQueue_Enqueue(&q, &r, 1000);
    TelemetryQueue_Dequeue(&q, NULL);
    if (!TelemetryQueue_Ack(&q, 1)) { FAIL("ack"); return; }
    if (TelemetryQueue_GetCount(&q) != 0) { FAIL("not removed"); return; }
    PASS();
}

static void test_overflow_drops_oldest(void)
{
    TelemetryQueue q; TelemetryQueue_Init(&q);
    for (uint64_t i = 1; i <= TELEMETRY_QUEUE_CAPACITY + 5; i++) {
        TelemetryRecord r = make_rec(i);
        TelemetryQueue_Enqueue(&q, &r, i * 1000);
    }
    /* Should have dropped 5 oldest */
    if (TelemetryQueue_GetDropCount(&q) != 5) { FAIL("drop count"); return; }
    if (TelemetryQueue_GetCount(&q) != TELEMETRY_QUEUE_CAPACITY) { FAIL("capacity"); return; }
    PASS();
}

static void test_ttl_expiry(void)
{
    TelemetryQueue q; TelemetryQueue_Init(&q);
    TelemetryRecord r = make_rec(1);
    TelemetryQueue_Enqueue(&q, &r, 1000);

    /* Tick after TTL — should expire */
    TelemetryQueue_Tick(&q, TELEMETRY_TTL_US + 2000);
    if (TelemetryQueue_GetCount(&q) != 0) { FAIL("should expire"); return; }
    PASS();
}

static void test_duplicate_rejected(void)
{
    TelemetryQueue q; TelemetryQueue_Init(&q);
    TelemetryRecord r = make_rec(1);
    TelemetryQueue_Enqueue(&q, &r, 1000);
    QueueStatus st = TelemetryQueue_Enqueue(&q, &r, 2000);
    if (st != QUEUE_DUPLICATE) { FAIL("should reject dup"); return; }
    PASS();
}

int main(void)
{
    printf("Telemetry Queue Tests\n");
    printf("─────────────────────\n");
    test_init_empty();
    test_enqueue_dequeue();
    test_ack_removes();
    test_overflow_drops_oldest();
    test_ttl_expiry();
    test_duplicate_rejected();
    printf("─────────────────────\n");
    printf("%d passed, %d failed\n",p,f);
    return f>0;
}
