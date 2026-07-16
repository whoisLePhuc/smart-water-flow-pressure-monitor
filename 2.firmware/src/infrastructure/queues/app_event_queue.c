#include "infrastructure/queues/app_event_queue.h"
#include <string.h>

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
        i = (uint16_t)(((uint32_t)i + 1u)
                       % (uint32_t)APP_EVENT_QUEUE_MAX_CAPACITY);
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
        return;
    if (!config) {
        const AppEventQueueConfig fallback_config = {
            .capacity = APP_EVENT_QUEUE_DEFAULT_CAPACITY,
            .reserved_critical = APP_EVENT_QUEUE_DEFAULT_RESERVED_CRITICAL,
            .reserved_measurement = APP_EVENT_QUEUE_DEFAULT_RESERVED_MEASUREMENT,
        };
        memset(queue, 0, sizeof(*queue));
        queue->config = fallback_config;
    } else {
        memset(queue, 0, sizeof(*queue));
        queue->config = *config;
    }

    /* Clamp capacity */
    if (queue->config.capacity > APP_EVENT_QUEUE_MAX_CAPACITY)
        queue->config.capacity = APP_EVENT_QUEUE_MAX_CAPACITY;
    if ((uint16_t)queue->config.reserved_critical > queue->config.capacity)
        queue->config.reserved_critical = (uint8_t)queue->config.capacity;
    if ((uint16_t)queue->config.reserved_measurement > queue->config.capacity)
        queue->config.reserved_measurement = (uint8_t)queue->config.capacity;
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

    /* True reservation: background events are capped at (capacity - reserved_critical),
     * ensuring critical events always have guaranteed slots.
     * Critical and measurement events can use full capacity when needed. */
    bool is_background = !is_priority_critical(event->priority)
                      && !is_priority_measurement(event->priority);

    if (is_background) {
        uint16_t max_background = (uint16_t)(queue->config.capacity
            - (uint16_t)queue->config.reserved_critical);
        if (queue->count >= max_background) {
            queue->overflow_count++;
            return EVENT_POST_BACKPRESSURE;
        }
    } else if (queue->count >= queue->config.capacity) {
        /* Queue completely full — critical event cannot be accepted */
        if (is_priority_critical(event->priority)) {
            queue->critical_emergency = true;
            return EVENT_POST_OVERFLOW_ESCALATED;
        }
        queue->overflow_count++;
        return EVENT_POST_BACKPRESSURE;
    }

    /* Enqueue */
    queue->buffer[queue->tail] = *event;
    queue->tail = (uint16_t)(((uint32_t)queue->tail + 1u)
                             % (uint32_t)APP_EVENT_QUEUE_MAX_CAPACITY);
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

    /* Fairness: if we've dequeued many high-priority events consecutively,
     * look for the oldest event of any priority instead of strictly highest.
     * Threshold: after 4 high-priority dequeues, serve lower priorities once. */
    bool force_fair_dequeue = queue->consecutive_dequeue_count >= 4;

    /* Priority dequeue: scan for highest-priority event (or oldest if fairness enforced) */
    uint16_t best_idx = queue->head;
    AppEventPriority best_prio = queue->buffer[queue->head].priority;

    uint16_t i = queue->head;
    uint16_t n = queue->count;
    while (n > 0) {
        if (!force_fair_dequeue && queue->buffer[i].priority < best_prio) {
            best_prio = (AppEventPriority)queue->buffer[i].priority;
            best_idx = i;
        }
        i = (uint16_t)(((uint32_t)i + 1u)
                       % (uint32_t)APP_EVENT_QUEUE_MAX_CAPACITY);
        n--;
    }

    /* Starvation detection: if fairness is forced but no lower-prio event exists,
     * count it as starvation evidence */
    if (force_fair_dequeue && best_prio == EVENT_PRIO_CRITICAL) {
        queue->starvation_count++;
    }

    /* Track consecutive dequeue priority for fairness */
    if (!force_fair_dequeue && best_prio <= EVENT_PRIO_SHARED_RESOURCE) {
        queue->consecutive_dequeue_count++;
    } else {
        queue->consecutive_dequeue_count = 0;
    }

    *event_out = queue->buffer[best_idx];

    /* Remove by shifting from best_idx to head-1 */
    uint16_t r = best_idx;
    uint16_t offset = (uint16_t)(((uint32_t)best_idx
                                  + (uint32_t)APP_EVENT_QUEUE_MAX_CAPACITY
                                  - (uint32_t)queue->head)
                                 % (uint32_t)APP_EVENT_QUEUE_MAX_CAPACITY);
    uint16_t remaining = (uint16_t)(queue->count - offset - 1u);
    while (remaining > 0) {
        uint16_t next = (uint16_t)(((uint32_t)r + 1u)
                                   % (uint32_t)APP_EVENT_QUEUE_MAX_CAPACITY);
        queue->buffer[r] = queue->buffer[next];
        r = next;
        remaining--;
    }

    queue->tail = (uint16_t)(((uint32_t)queue->tail
                              + (uint32_t)APP_EVENT_QUEUE_MAX_CAPACITY - 1u)
                             % (uint32_t)APP_EVENT_QUEUE_MAX_CAPACITY);
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

uint32_t app_event_queue_get_starvation_count(const AppEventQueue *queue)
{
    return queue ? queue->starvation_count : 0;
}
