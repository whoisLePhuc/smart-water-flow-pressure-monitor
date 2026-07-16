#ifndef SWFPM_LINUX_SCHEDULED_ACTION_QUEUE_H
#define SWFPM_LINUX_SCHEDULED_ACTION_QUEUE_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Deterministic scheduled-action queue for Linux platform backend.
 *
 * Actions are dispatched in strict total order:
 *   due_us → action_class → resource_id → resource_generation →
 *   source_sequence → insertion_sequence
 *
 * This ensures deterministic ordering regardless of insertion order.
 */

#define ACTION_QUEUE_MAX_CAPACITY 64

typedef enum {
    ACTION_CLASS_SPI_COMPLETION,
    ACTION_CLASS_I2C_COMPLETION,
    ACTION_CLASS_GPIO_EVIDENCE,
    ACTION_CLASS_RTC_ALARM,
    ACTION_CLASS_WAKE,
    ACTION_CLASS_RESET,
    ACTION_CLASS_USER_0,  /* Reserved for scenario injection */
    ACTION_CLASS_USER_1,
} ActionClass;

typedef struct {
    uint64_t due_us;                /* Absolute monotonic deadline */
    ActionClass action_class;
    uint32_t   resource_id;
    uint32_t   resource_generation;
    uint32_t   source_sequence;
    uint64_t   insertion_sequence;  /* Auto-assigned, monotonic */
    uint32_t   operation_id;
    uint32_t   correlation_id;
    uint32_t   owner_generation;
    uint32_t   detail_flags;        /* Type-specific payload */
    uint8_t    payload[16];
    uint8_t    payload_size;
} LinuxScheduledAction;

typedef struct {
    LinuxScheduledAction actions[ACTION_QUEUE_MAX_CAPACITY];
    uint16_t             count;
    uint64_t             next_insertion_seq;
    uint64_t             total_dispatched;
    uint32_t             overflow_count;
    uint32_t             cancel_count;
    uint32_t             stale_cancel_count;
} LinuxScheduledActionQueue;


void action_queue_init(LinuxScheduledActionQueue *queue);


bool action_queue_schedule(LinuxScheduledActionQueue *queue,
                           const LinuxScheduledAction *action);

/* Cancel by action_id + expected_generation. Returns true if cancelled. */
bool action_queue_cancel(LinuxScheduledActionQueue *queue,
                         uint32_t operation_id,
                         uint32_t expected_generation);


/* Dispatch all actions due at or before now_us.
 * Returns number of actions dispatched (written to out). */
uint16_t action_queue_dispatch_due(LinuxScheduledActionQueue *queue,
                                   uint64_t now_us,
                                   LinuxScheduledAction *out,
                                   uint16_t max_out);

/* Get earliest deadline, returns false if queue empty. */
bool action_queue_next_deadline(const LinuxScheduledActionQueue *queue,
                                uint64_t *deadline_us);


uint16_t action_queue_get_count(const LinuxScheduledActionQueue *queue);
uint32_t action_queue_get_overflow(const LinuxScheduledActionQueue *queue);

#endif /* SWFPM_LINUX_SCHEDULED_ACTION_QUEUE_H */
