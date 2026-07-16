#ifndef SWFPM_APP_EVENT_QUEUE_H
#define SWFPM_APP_EVENT_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include "infrastructure/event/event_id.h"
#include "ports/critical_section_port.h"


typedef enum {
    DELIVERY_EDGE,          /* Every occurrence is retained. */
    DELIVERY_COMPLETION,    /* Exactly one terminal outcome per request. */
    DELIVERY_LEVEL,         /* Latest condition may coalesce a pending event. */
    DELIVERY_DEADLINE,      /* Scheduler event validated by job generation. */
    DELIVERY_MAILBOX        /* Event signals data stored in the owner's mailbox. */
} AppEventDelivery;


typedef enum {
    EVENT_PRIO_CRITICAL = 0,    /* Integrity, fault — never silently dropped */
    EVENT_PRIO_MEASUREMENT,     /* Sensor completion, deadline */
    EVENT_PRIO_SHARED_RESOURCE, /* Bus/transaction completion */
    EVENT_PRIO_CONFIG,          /* Config apply, time, reporting */
    EVENT_PRIO_BACKGROUND,      /* Telemetry, display, diagnostics */
    EVENT_PRIO_COUNT
} AppEventPriority;


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
    uint8_t payload[EVENT_MAX_PAYLOAD_SIZE]; /* Queue-owned copy after post. */
    uint8_t payload_size;                    /* Must not exceed payload capacity. */
} AppEvent;


typedef enum {
    EVENT_POST_OK,
    EVENT_POST_COALESCED,        /* Level/deadline coalesced with pending */
    EVENT_POST_BACKPRESSURE,     /* Queue full, producer must retry */
    EVENT_POST_REJECTED_INVALID, /* Metadata invalid */
    EVENT_POST_OVERFLOW_ESCALATED /* Critical event — emergency flag set */
} EventPostResult;


typedef struct {
    uint16_t capacity;             /* Total event capacity */
    uint8_t  reserved_critical;    /* Slots reserved for PRIO_CRITICAL */
    uint8_t  reserved_measurement; /* Slots reserved for PRIO_MEASUREMENT */
} AppEventQueueConfig;


#define APP_EVENT_QUEUE_MAX_CAPACITY 64

typedef struct {
    AppEvent             buffer[APP_EVENT_QUEUE_MAX_CAPACITY];
    AppEventQueueConfig  config;
    uint16_t             head;
    uint16_t             tail;
    uint16_t             count;
    bool critical_emergency; /* Sticky when a critical event cannot be admitted. */
    uint32_t overflow_count; /* Monotonic count of full-queue post attempts. */
    uint32_t drop_count;     /* Monotonic count of events rejected by policy. */
    uint32_t starvation_count; /* Low-priority work skipped for fairness. */
    uint8_t consecutive_dequeue_count; /* Reset when the priority band changes. */
    CriticalSectionPort critical_section; /* Optional platform-owned IRQ lock. */
} AppEventQueue;

#define APP_EVENT_QUEUE_DEFAULT_CAPACITY          32
#define APP_EVENT_QUEUE_DEFAULT_RESERVED_CRITICAL  4
#define APP_EVENT_QUEUE_DEFAULT_RESERVED_MEASUREMENT 4


void app_event_queue_init(AppEventQueue *queue, const AppEventQueueConfig *config);

// Binds the platform interrupt-mask/lock implementation. Binding is accepted
// only while the queue is empty; the port is copied and may be stack-created.
bool app_event_queue_bind_critical_section(
    AppEventQueue *queue, const CriticalSectionPort *port);

EventPostResult app_event_queue_post(AppEventQueue *queue, const AppEvent *event);

// ISR-safe admission contract: copies the bounded payload and never invokes a
// consumer. Platform code must still provide the required critical section.
EventPostResult app_event_queue_post_from_isr(AppEventQueue *queue, const AppEvent *event);

// Selects the highest eligible priority while applying the fairness budget.
// event_out receives an independent copy owned by the caller.
bool app_event_queue_try_get(AppEventQueue *queue, AppEvent *event_out);

uint16_t app_event_queue_get_count(const AppEventQueue *queue);

uint32_t app_event_queue_get_overflow_count(const AppEventQueue *queue);

uint32_t app_event_queue_get_drop_count(const AppEventQueue *queue);

uint32_t app_event_queue_get_starvation_count(const AppEventQueue *queue);

#endif /* SWFPM_APP_EVENT_QUEUE_H */
