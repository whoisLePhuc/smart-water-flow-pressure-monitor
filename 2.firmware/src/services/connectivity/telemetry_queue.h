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
    uint16_t head;          /* Oldest eligible record. */
    uint16_t tail;          /* Next insertion slot. */
    uint16_t in_flight_idx; /* Valid only while has_in_flight is true. */
    bool has_in_flight;     /* Enforces one delivery operation at a time. */
    uint64_t drop_count;    /* Monotonic diagnostic counter. */
} TelemetryQueue;

void TelemetryQueue_Init(TelemetryQueue *q);

QueueStatus TelemetryQueue_Enqueue(TelemetryQueue *q,
                                    const TelemetryRecord *rec,
                                    uint64_t now_monotonic_us);

// Marks the oldest eligible entry in flight and copies its immutable record.
// The entry remains in the queue until Ack succeeds or policy drops it.
bool TelemetryQueue_Dequeue(TelemetryQueue *q, TelemetryRecord *rec_out);

// Removes only the current in-flight record with the matching sequence.
// Duplicate or stale acknowledgements leave the queue unchanged.
bool TelemetryQueue_Ack(TelemetryQueue *q, uint64_t record_sequence);

// Expires records by monotonic age. Wall-clock corrections do not affect TTL.
void TelemetryQueue_Tick(TelemetryQueue *q, uint64_t now_monotonic_us);

uint16_t TelemetryQueue_GetCount(const TelemetryQueue *q);
bool     TelemetryQueue_HasInFlight(const TelemetryQueue *q);
uint64_t TelemetryQueue_GetDropCount(const TelemetryQueue *q);

#endif
