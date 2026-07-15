#include "services/cellular_delivery.h"
#include "services/telemetry_queue.h"
#include <stdio.h>
#include <string.h>

static int p=0,f=0;
#define T(n) printf("  %-45s ",n)
#define PASS() do{p++;printf("PASS\n");}while(0)
#define FAIL(m) do{f++;printf("FAIL: %s\n",m);}while(0)

static TelemetryRecord mkrec(uint64_t seq)
{
    TelemetryRecord r; memset(&r,0,sizeof(r)); r.record_sequence=seq; return r;
}

static void test_init_idle(void)
{
    CellularDeliveryService d; CellularDelivery_Init(&d,0);
    if (d.state != DEL_IDLE) { FAIL("not IDLE"); return; }
    if (d.conn_status != CONN_NOT_READY) { FAIL("not NOT_READY"); return; }
    PASS();
}

static void test_connect_then_attempt(void)
{
    CellularDeliveryService d; CellularDelivery_Init(&d,0);
    TelemetryQueue q; TelemetryQueue_Init(&q);
    TelemetryRecord r1 = mkrec(1);
    TelemetryQueue_Enqueue(&q, &r1, 1000);

    CellularDelivery_Connect(&d, 0);
    if (!CellularDelivery_StartAttempt(&d, &q, 1000)) { FAIL("start"); return; }
    if (d.state != DEL_SENDING) { FAIL("not SENDING"); return; }
    PASS();
}

static void test_ack_completes(void)
{
    CellularDeliveryService d; CellularDelivery_Init(&d,0);
    TelemetryQueue q; TelemetryQueue_Init(&q);
    TelemetryRecord r1 = mkrec(1);
    TelemetryQueue_Enqueue(&q, &r1, 1000);

    CellularDelivery_Connect(&d, 0);
    CellularDelivery_StartAttempt(&d, &q, 1000);
    CellularDelivery_Complete(&d, 0, &q, 2000);

    if (d.state != DEL_IDLE) { FAIL("not IDLE after ACK"); return; }
    PASS();
}

static void test_retry_then_limit(void)
{
    CellularDeliveryService d; CellularDelivery_Init(&d,0);
    TelemetryQueue q; TelemetryQueue_Init(&q);
    TelemetryRecord r1 = mkrec(1);
    TelemetryQueue_Enqueue(&q, &r1, 1000);

    CellularDelivery_Connect(&d, 0);
    CellularDelivery_StartAttempt(&d, &q, 1000); /* attempt 1 */

    /* 3 retries */
    CellularDelivery_Complete(&d, 2, &q, 2000); /* Transport failed → retry wait */
    CellularDelivery_Tick(&d, &q, 2000 + DELIVERY_RETRY_DELAY_US); /* attempt 2 */

    CellularDelivery_Complete(&d, 2, &q, 3000);
    CellularDelivery_Tick(&d, &q, 3000 + DELIVERY_RETRY_DELAY_US); /* attempt 3 */

    CellularDelivery_Complete(&d, 2, &q, 4000);
    CellularDelivery_Tick(&d, &q, 4000 + DELIVERY_RETRY_DELAY_US); /* attempt 4 — should NOT retry */

    if (d.state != DEL_IDLE) { FAIL("should stop after 3 retries"); return; }
    PASS();
}

int main(void)
{
    printf("Delivery Service Tests\n");
    printf("──────────────────────\n");
    test_init_idle();
    test_connect_then_attempt();
    test_ack_completes();
    test_retry_then_limit();
    printf("──────────────────────\n");
    printf("%d passed, %d failed\n",p,f);
    return f>0;
}
