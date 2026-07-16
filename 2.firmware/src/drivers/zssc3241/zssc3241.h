#ifndef SWFPM_ZSSC3241_H
#define SWFPM_ZSSC3241_H

#include <stdint.h>
#include <stdbool.h>
#include "domain/common/metadata.h"
#include "infrastructure/event/event_id.h"
#include "infrastructure/queues/app_event_queue.h"

typedef enum {
    ZSSC_STATE_IDLE,
    ZSSC_STATE_SLEEP,
    ZSSC_STATE_STARTING,
    ZSSC_STATE_WAITING_EOC,
    ZSSC_STATE_POLLING,
    ZSSC_STATE_READING,
    ZSSC_STATE_RAW_READY,
    ZSSC_STATE_TIMEOUT,
    ZSSC_STATE_RECOVERY
} ZsscDriverState;

typedef struct {
    uint32_t generation;      /* Increment to invalidate pending completions. */
    uint64_t sample_sequence; /* Monotonic sample identity. */
    ZsscDriverState state;
    uint32_t active_correlation_id; /* Expected integration operation. */
    uint32_t active_transaction_id; /* Expected I2C bus transaction. */
    uint64_t        sample_monotonic_us;

    AppEventQueue *event_queue; /* Borrowed; owner must outlive the driver. */

    uint32_t sample_due_count;   /* Monotonic diagnostic counter. */
    uint32_t eoc_received_count; /* Monotonic diagnostic counter. */
    uint32_t raw_ready_count;    /* Monotonic diagnostic counter. */
    uint32_t timeout_count;      /* Monotonic diagnostic counter. */
    uint32_t error_count;        /* Monotonic diagnostic counter. */
} Zssc3241Driver;

void zssc3241_init(Zssc3241Driver *driver, AppEventQueue *event_queue);

void zssc3241_on_sample_due(Zssc3241Driver *driver, uint64_t now_us);

void zssc3241_on_eoc_asserted(Zssc3241Driver *driver, uint64_t now_us);

void zssc3241_on_poll_due(Zssc3241Driver *driver, uint64_t now_us);

// Consumes rx_data only during this call. The current skeleton emits RAW_READY
// but does not parse or retain a coherent pressure payload.
void zssc3241_on_i2c_completion(Zssc3241Driver *driver,
                                 uint32_t correlation_id,
                                 bool success,
                                 const uint8_t *rx_data,
                                 uint16_t rx_length,
                                 uint64_t now_us);

void zssc3241_on_timeout(Zssc3241Driver *driver, uint64_t now_us);

#endif
