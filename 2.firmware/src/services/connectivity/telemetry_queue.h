#ifndef SWFPM_TELEMETRY_QUEUE_H
#define SWFPM_TELEMETRY_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include "protocols/telemetry/telemetry_builder.h"

#define TELEMETRY_QUEUE_CAPACITY 64u
#define TELEMETRY_TTL_US         (24ULL * 3600ULL * 1000000ULL)

typedef enum {
    QUEUE_OK,
    QUEUE_FULL,
    QUEUE_DUPLICATE,
    QUEUE_INVALID
} QueueStatus;

typedef enum {
    QREC_BUILT,
    QREC_QUEUED,
    QREC_IN_FLIGHT,
    QREC_ACKNOWLEDGED,
    QREC_DROPPED
} QueueRecordState;

typedef struct {
    TelemetryRecord  record;
    uint64_t         enqueue_monotonic_us;
    QueueRecordState state;
    uint8_t          retry_count;
    uint64_t         last_attempt_us;
} QueueEntry;

typedef struct {
    QueueEntry entries[TELEMETRY_QUEUE_CAPACITY];
    uint16_t   count;
    uint16_t   head;     /* oldest eligible */
    uint16_t   tail;     /* next free slot */
    uint16_t   in_flight_idx;
    bool       has_in_flight;
    uint64_t   drop_count;
} TelemetryQueue;

void TelemetryQueue_Init(TelemetryQueue *q);

QueueStatus TelemetryQueue_Enqueue(TelemetryQueue *q,
                                    const TelemetryRecord *rec,
                                    uint64_t now_monotonic_us);

bool TelemetryQueue_Dequeue(TelemetryQueue *q, TelemetryRecord *rec_out);

bool TelemetryQueue_Ack(TelemetryQueue *q, uint64_t record_sequence);

void TelemetryQueue_Tick(TelemetryQueue *q, uint64_t now_monotonic_us);

uint16_t TelemetryQueue_GetCount(const TelemetryQueue *q);
bool     TelemetryQueue_HasInFlight(const TelemetryQueue *q);
uint64_t TelemetryQueue_GetDropCount(const TelemetryQueue *q);

#endif
