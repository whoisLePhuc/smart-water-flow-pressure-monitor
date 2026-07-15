#include "telemetry_queue.h"
#include <string.h>

void TelemetryQueue_Init(TelemetryQueue *q)
{
    memset(q, 0, sizeof(*q));
}

QueueStatus TelemetryQueue_Enqueue(TelemetryQueue *q,
                                    const TelemetryRecord *rec,
                                    uint64_t now_us)
{
    if (!q || !rec) return QUEUE_INVALID;

    /* Check duplicate */
    for (uint16_t i = 0; i < TELEMETRY_QUEUE_CAPACITY; i++) {
        if (q->entries[i].state != QREC_BUILT &&
            q->entries[i].state != QREC_DROPPED &&
            q->entries[i].record.record_sequence == rec->record_sequence) {
            return QUEUE_DUPLICATE;
        }
    }

    /* Check if room exists, try to expire old records first */
    if (q->count >= TELEMETRY_QUEUE_CAPACITY) {
        TelemetryQueue_Tick(q, now_us);
    }

    if (q->count >= TELEMETRY_QUEUE_CAPACITY) {
        /* Drop oldest non-in-flight */
        uint16_t oldest = TELEMETRY_QUEUE_CAPACITY;
        uint64_t oldest_time = now_us;
        for (uint16_t i = 0; i < TELEMETRY_QUEUE_CAPACITY; i++) {
            if (q->entries[i].state == QREC_BUILT ||
                q->entries[i].state == QREC_QUEUED) {
                if (q->entries[i].enqueue_monotonic_us < oldest_time) {
                    oldest_time = q->entries[i].enqueue_monotonic_us;
                    oldest = i;
                }
            }
        }

        if (oldest == TELEMETRY_QUEUE_CAPACITY) {
            return QUEUE_FULL;  /* everything is in-flight */
        }

        q->entries[oldest].state = QREC_DROPPED;
        q->drop_count++;
        q->count--;
    }

    /* Find free slot — use tail for FIFO */
    q->entries[q->tail].record = *rec;
    q->entries[q->tail].enqueue_monotonic_us = now_us;
    q->entries[q->tail].state = QREC_QUEUED;
    q->entries[q->tail].retry_count = 0;
    q->entries[q->tail].last_attempt_us = 0;

    q->count++;
    q->tail = (q->tail + 1) % TELEMETRY_QUEUE_CAPACITY;

    return QUEUE_OK;
}

bool TelemetryQueue_Dequeue(TelemetryQueue *q, TelemetryRecord *rec_out)
{
    if (!q || q->has_in_flight || q->count == 0) return false;

    /* Find oldest QUEUED record */
    uint16_t oldest = TELEMETRY_QUEUE_CAPACITY;
    uint64_t oldest_time = (uint64_t)-1;
    for (uint16_t i = 0; i < TELEMETRY_QUEUE_CAPACITY; i++) {
        if (q->entries[i].state == QREC_QUEUED) {
            if (q->entries[i].enqueue_monotonic_us < oldest_time) {
                oldest_time = q->entries[i].enqueue_monotonic_us;
                oldest = i;
            }
        }
    }

    if (oldest == TELEMETRY_QUEUE_CAPACITY) return false;

    q->entries[oldest].state = QREC_IN_FLIGHT;
    q->in_flight_idx = oldest;
    q->has_in_flight = true;

    if (rec_out)
        *rec_out = q->entries[oldest].record;

    return true;
}

bool TelemetryQueue_Ack(TelemetryQueue *q, uint64_t record_sequence)
{
    if (!q) return false;

    for (uint16_t i = 0; i < TELEMETRY_QUEUE_CAPACITY; i++) {
        if ((q->entries[i].state == QREC_IN_FLIGHT ||
             q->entries[i].state == QREC_QUEUED) &&
            q->entries[i].record.record_sequence == record_sequence) {
            q->entries[i].state = QREC_ACKNOWLEDGED;
            q->count--;
            if (q->has_in_flight && q->in_flight_idx == i) {
                q->has_in_flight = false;
            }
            return true;
        }
    }
    return false;
}

void TelemetryQueue_Tick(TelemetryQueue *q, uint64_t now_us)
{
    if (!q) return;

    for (uint16_t i = 0; i < TELEMETRY_QUEUE_CAPACITY; i++) {
        if (q->entries[i].state == QREC_QUEUED ||
            q->entries[i].state == QREC_BUILT) {
            if (q->entries[i].enqueue_monotonic_us > 0 &&
                (now_us - q->entries[i].enqueue_monotonic_us) >= TELEMETRY_TTL_US) {
                q->entries[i].state = QREC_DROPPED;
                q->drop_count++;
                q->count--;
            }
        }
    }
}

uint16_t TelemetryQueue_GetCount(const TelemetryQueue *q)
{
    return q ? q->count : 0;
}

bool TelemetryQueue_HasInFlight(const TelemetryQueue *q)
{
    return q ? q->has_in_flight : false;
}

uint64_t TelemetryQueue_GetDropCount(const TelemetryQueue *q)
{
    return q ? q->drop_count : 0;
}
