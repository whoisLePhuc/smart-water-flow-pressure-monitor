#include "app_event_queue.h"
#include <string.h>

/* =================================================================
 * Default instance
 * ================================================================= */

static AppEventQueue default_queue;

/* =================================================================
 * Helpers
 * ================================================================= */

static bool is_priority_critical(AppEventPriority prio)
{
    return prio == EVENT_PRIO_CRITICAL;
}

static bool is_priority_measurement(AppEventPriority prio)
{
    return prio == EVENT_PRIO_MEASUREMENT;
}

static bool can_coalesce(AppEventDelivery delivery)
{
    return delivery == DELIVERY_LEVEL;
}

/* Check if an equivalent level event is already pending */
static bool find_coalesce_target(const AppEventQueue *q, const AppEvent *evt, uint16_t *idx_out)
{
    if (!can_coalesce(evt->delivery))
        return false;

    uint16_t i = q->head;
    uint16_t n = q->count;
    while (n > 0) {
        if (q->buffer[i].id == evt->id &&
            q->buffer[i].source_id == evt->source_id &&
            q->buffer[i].delivery == DELIVERY_LEVEL) {
            *idx_out = i;
            return true;
        }
        i = (i + 1) % APP_EVENT_QUEUE_MAX_CAPACITY;
        n--;
    }
    return false;
}

/* =================================================================
 * API implementation
 * ================================================================= */

void app_event_queue_init(AppEventQueue *queue, const AppEventQueueConfig *config)
{
    if (!queue)
        queue = &default_queue;
    if (!config) {
        static const AppEventQueueConfig default_cfg = {
            .capacity = APP_EVENT_QUEUE_DEFAULT_CAPACITY,
            .reserved_critical = APP_EVENT_QUEUE_DEFAULT_RESERVED_CRITICAL,
            .reserved_measurement = APP_EVENT_QUEUE_DEFAULT_RESERVED_MEASUREMENT,
        };
        config = &default_cfg;
    }

    memset(queue, 0, sizeof(*queue));
    queue->config = *config;

    /* Clamp capacity */
    if (queue->config.capacity > APP_EVENT_QUEUE_MAX_CAPACITY)
        queue->config.capacity = APP_EVENT_QUEUE_MAX_CAPACITY;
}

EventPostResult app_event_queue_post(AppEventQueue *queue, const AppEvent *event)
{
    if (!queue || !event)
        return EVENT_POST_REJECTED_INVALID;

    /* Attempt coalescing for level events */
    if (can_coalesce(event->delivery)) {
        uint16_t idx;
        if (find_coalesce_target(queue, event, &idx)) {
            queue->buffer[idx] = *event;
            return EVENT_POST_COALESCED;
        }
    }

    /* Check reserved capacity for critical events */
    if (is_priority_critical(event->priority)) {
        uint16_t critical_count = 0;
        uint16_t i = queue->head;
        uint16_t n = queue->count;
        while (n > 0) {
            if (is_priority_critical(queue->buffer[i].priority))
                critical_count++;
            i = (i + 1) % APP_EVENT_QUEUE_MAX_CAPACITY;
            n--;
        }
        if (critical_count >= queue->config.reserved_critical) {
            queue->critical_emergency = true;
            queue->overflow_count++;
            return EVENT_POST_OVERFLOW_ESCALATED;
        }
    }

    /* Check measurement reserved capacity */
    if (is_priority_measurement(event->priority)) {
        uint16_t meas_count = 0;
        uint16_t i = queue->head;
        uint16_t n = queue->count;
        while (n > 0) {
            if (is_priority_measurement(queue->buffer[i].priority))
                meas_count++;
            i = (i + 1) % APP_EVENT_QUEUE_MAX_CAPACITY;
            n--;
        }
        if (meas_count >= queue->config.reserved_measurement) {
            queue->overflow_count++;
            return EVENT_POST_BACKPRESSURE;
        }
    }

    /* General capacity check */
    if (queue->count >= queue->config.capacity) {
        queue->overflow_count++;
        return EVENT_POST_BACKPRESSURE;
    }

    /* Enqueue */
    queue->buffer[queue->tail] = *event;
    queue->tail = (queue->tail + 1) % APP_EVENT_QUEUE_MAX_CAPACITY;
    queue->count++;

    return EVENT_POST_OK;
}

EventPostResult app_event_queue_post_from_isr(AppEventQueue *queue, const AppEvent *event)
{
    /* Phase 1: same as post (critical-section wrapping will be added
     * when ISR context is defined for the target platform) */
    return app_event_queue_post(queue, event);
}

bool app_event_queue_try_get(AppEventQueue *queue, AppEvent *event_out)
{
    if (!queue || !event_out || queue->count == 0)
        return false;

    /* Priority dequeue: scan for highest-priority event */
    uint16_t best_idx = queue->head;
    AppEventPriority best_prio = queue->buffer[queue->head].priority;

    uint16_t i = queue->head;
    uint16_t n = queue->count;
    while (n > 0) {
        if (queue->buffer[i].priority < best_prio) {
            best_prio = (AppEventPriority)queue->buffer[i].priority;
            best_idx = i;
        }
        i = (i + 1) % APP_EVENT_QUEUE_MAX_CAPACITY;
        n--;
    }

    *event_out = queue->buffer[best_idx];

    /* Remove by shifting from best_idx to head-1 */
    uint16_t r = best_idx;
    uint16_t remaining = queue->count - (uint16_t)((best_idx - queue->head + APP_EVENT_QUEUE_MAX_CAPACITY) % APP_EVENT_QUEUE_MAX_CAPACITY) - 1;
    while (remaining > 0) {
        uint16_t next = (r + 1) % APP_EVENT_QUEUE_MAX_CAPACITY;
        queue->buffer[r] = queue->buffer[next];
        r = next;
        remaining--;
    }

    queue->tail = (queue->tail - 1 + APP_EVENT_QUEUE_MAX_CAPACITY) % APP_EVENT_QUEUE_MAX_CAPACITY;
    queue->count--;

    return true;
}

uint16_t app_event_queue_get_count(const AppEventQueue *queue)
{
    return queue ? queue->count : 0;
}

uint32_t app_event_queue_get_overflow_count(const AppEventQueue *queue)
{
    return queue ? queue->overflow_count : 0;
}

uint32_t app_event_queue_get_drop_count(const AppEventQueue *queue)
{
    return queue ? queue->drop_count : 0;
}
