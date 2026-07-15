#ifndef SWFPM_CELLULAR_DELIVERY_H
#define SWFPM_CELLULAR_DELIVERY_H

#include <stdint.h>
#include <stdbool.h>
#include "telemetry_queue.h"

#define DELIVERY_RETRY_DELAY_US  (30ULL * 1000000ULL)
#define DELIVERY_MAX_RETRIES     3u

typedef enum {
    DEL_IDLE,
    DEL_CONNECTING,
    DEL_SENDING,
    DEL_WAIT_RESPONSE,
    DEL_RETRY_WAIT,
    DEL_COMPLETE,
    DEL_FAILED
} DeliveryState;

typedef struct {
    DeliveryState state;
    ConnectivityStatus conn_status;
    uint32_t      connection_generation;
    uint64_t      current_record_seq;
    uint8_t       attempt_count;
    uint64_t      attempt_start_us;
    uint64_t      retry_at_us;
    uint64_t      diag_attempts;
    uint64_t      diag_success;
    uint64_t      diag_timeout;
    uint64_t      diag_rejected;
    uint64_t      diag_stale;
} CellularDeliveryService;

void CellularDelivery_Init(CellularDeliveryService *svc, uint64_t now_us);
void CellularDelivery_Connect(CellularDeliveryService *svc, uint64_t now_us);
void CellularDelivery_Disconnect(CellularDeliveryService *svc);
bool CellularDelivery_StartAttempt(CellularDeliveryService *svc, TelemetryQueue *queue, uint64_t now_us);
void CellularDelivery_Complete(CellularDeliveryService *svc, uint8_t outcome, TelemetryQueue *queue, uint64_t now_us);
void CellularDelivery_Tick(CellularDeliveryService *svc, TelemetryQueue *queue, uint64_t now_us);
bool CellularDelivery_IsIdle(const CellularDeliveryService *svc);

#endif
