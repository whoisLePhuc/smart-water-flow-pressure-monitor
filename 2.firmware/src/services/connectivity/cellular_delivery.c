#include "services/connectivity/cellular_delivery.h"
#include <string.h>

void CellularDelivery_Init(CellularDeliveryService *svc, uint64_t now_us)
{
    (void)now_us;
    memset(svc, 0, sizeof(*svc));
    svc->state = DEL_IDLE;
    svc->conn_status = CONN_NOT_READY;
    svc->connection_generation = 1;
}

void CellularDelivery_Connect(CellularDeliveryService *svc, uint64_t now_us)
{
    (void)now_us;
    if (!svc) return;
    svc->conn_status = CONN_ONLINE;
    svc->state = DEL_IDLE;
}

void CellularDelivery_Disconnect(CellularDeliveryService *svc)
{
    if (!svc) return;
    svc->conn_status = CONN_OFFLINE;
    svc->connection_generation++;
    svc->state = DEL_IDLE;
}

bool CellularDelivery_StartAttempt(CellularDeliveryService *svc, TelemetryQueue *queue, uint64_t now_us)
{
    if (!svc || !queue) return false;
    if (svc->state != DEL_IDLE) return false;
    if (svc->conn_status != CONN_ONLINE) return false;

    TelemetryRecord rec;
    if (!TelemetryQueue_Dequeue(queue, &rec)) return false;

    svc->state = DEL_SENDING;
    svc->current_record_seq = rec.record_sequence;
    svc->attempt_count++;
    svc->attempt_start_us = now_us;
    svc->diag_attempts++;
    return true;
}

void CellularDelivery_Complete(CellularDeliveryService *svc, uint8_t outcome, TelemetryQueue *queue, uint64_t now_us)
{
    if (!svc) return;

    switch (outcome) {
    case 0: /* ACKNOWLEDGED */
        if (queue && svc->current_record_seq > 0) {
            TelemetryQueue_Ack(queue, svc->current_record_seq);
        }
        svc->diag_success++;
        svc->state = DEL_IDLE;
        svc->attempt_count = 0;
        break;

    case 1: /* REJECTED_BY_SERVER */
        if (queue && svc->current_record_seq > 0) {
            TelemetryQueue_Ack(queue, svc->current_record_seq);
        }
        svc->diag_rejected++;
        svc->state = DEL_IDLE;
        svc->attempt_count = 0;
        break;

    case 2: /* TRANSPORT_FAILED */
    case 3: /* TIMEOUT */
        if (svc->attempt_count >= DELIVERY_MAX_RETRIES) {
            svc->state = DEL_IDLE;
            svc->attempt_count = 0;
        } else {
            svc->state = DEL_RETRY_WAIT;
            svc->retry_at_us = now_us + DELIVERY_RETRY_DELAY_US;
        }
        if (outcome == 3) svc->diag_timeout++;
        break;

    default: /* OUTCOME_UNKNOWN / CANCELLED */
        svc->state = DEL_IDLE;
        svc->attempt_count = 0;
        break;
    }
}

void CellularDelivery_Tick(CellularDeliveryService *svc, TelemetryQueue *queue, uint64_t now_us)
{
    if (!svc) return;

    if (svc->state == DEL_RETRY_WAIT && now_us >= svc->retry_at_us) {
        svc->state = DEL_IDLE;
        /* Auto-retry */
        CellularDelivery_StartAttempt(svc, queue, now_us);
    }
}

bool CellularDelivery_IsIdle(const CellularDeliveryService *svc)
{
    return svc ? (svc->state == DEL_IDLE) : true;
}
