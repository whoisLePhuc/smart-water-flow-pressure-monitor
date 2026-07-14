#ifndef SWFPM_APP_EVENT_QUEUE_H
#define SWFPM_APP_EVENT_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include "data_model.h"

/* =================================================================
 * Delivery classes
 * ================================================================= */

typedef enum {
    DELIVERY_EDGE,          /* Every occurrence matters — queued */
    DELIVERY_COMPLETION,    /* Terminal outcome of a request — queued */
    DELIVERY_LEVEL,         /* Current condition — MAY be coalesced */
    DELIVERY_DEADLINE,      /* Job due — queued with generation check */
    DELIVERY_MAILBOX        /* Payload in owner mailbox — ready flag */
} AppEventDelivery;

/* =================================================================
 * Priority classes (logical, NOT NVIC/RTOS priorities)
 * ================================================================= */

typedef enum {
    EVENT_PRIO_CRITICAL = 0,    /* Integrity, fault — never silently dropped */
    EVENT_PRIO_MEASUREMENT,     /* Sensor completion, deadline */
    EVENT_PRIO_SHARED_RESOURCE, /* Bus/transaction completion */
    EVENT_PRIO_CONFIG,          /* Config apply, time, reporting */
    EVENT_PRIO_BACKGROUND,      /* Telemetry, display, diagnostics */
    EVENT_PRIO_COUNT
} AppEventPriority;

/* =================================================================
 * Event envelope
 * ================================================================= */

#define EVENT_MAX_PAYLOAD_SIZE 16

typedef struct {
    EventId            id;
    uint32_t           source_id;
    AppEventPriority   priority;
    AppEventDelivery   delivery;
    uint32_t           sequence;
    uint32_t           correlation_id;
    uint32_t           source_generation;
    uint64_t           monotonic_timestamp_us;
    uint8_t            payload[EVENT_MAX_PAYLOAD_SIZE];
    uint8_t            payload_size;
} AppEvent;

/* =================================================================
 * Post results
 * ================================================================= */

typedef enum {
    EVENT_POST_OK,
    EVENT_POST_COALESCED,        /* Level/deadline coalesced with pending */
    EVENT_POST_BACKPRESSURE,     /* Queue full, producer must retry */
    EVENT_POST_REJECTED_INVALID, /* Metadata invalid */
    EVENT_POST_OVERFLOW_ESCALATED /* Critical event — emergency flag set */
} EventPostResult;

/* =================================================================
 * Configuration
 * ================================================================= */

typedef struct {
    uint16_t capacity;             /* Total event capacity */
    uint8_t  reserved_critical;    /* Slots reserved for PRIO_CRITICAL */
    uint8_t  reserved_measurement; /* Slots reserved for PRIO_MEASUREMENT */
} AppEventQueueConfig;

/* =================================================================
 * Queue — exposed struct (embedded target: static allocation only)
 * ================================================================= */

#define APP_EVENT_QUEUE_MAX_CAPACITY 64

typedef struct {
    AppEvent             buffer[APP_EVENT_QUEUE_MAX_CAPACITY];
    AppEventQueueConfig  config;
    uint16_t             head;
    uint16_t             tail;
    uint16_t             count;
    bool                 critical_emergency;
    uint32_t             overflow_count;
    uint32_t             drop_count;
} AppEventQueue;

#define APP_EVENT_QUEUE_DEFAULT_CAPACITY          32
#define APP_EVENT_QUEUE_DEFAULT_RESERVED_CRITICAL  4
#define APP_EVENT_QUEUE_DEFAULT_RESERVED_MEASUREMENT 4

/* =================================================================
 * API
 * ================================================================= */

void app_event_queue_init(AppEventQueue *queue, const AppEventQueueConfig *config);

EventPostResult app_event_queue_post(AppEventQueue *queue, const AppEvent *event);

EventPostResult app_event_queue_post_from_isr(AppEventQueue *queue, const AppEvent *event);

bool app_event_queue_try_get(AppEventQueue *queue, AppEvent *event_out);

uint16_t app_event_queue_get_count(const AppEventQueue *queue);

uint32_t app_event_queue_get_overflow_count(const AppEventQueue *queue);

uint32_t app_event_queue_get_drop_count(const AppEventQueue *queue);

#endif /* SWFPM_APP_EVENT_QUEUE_H */
