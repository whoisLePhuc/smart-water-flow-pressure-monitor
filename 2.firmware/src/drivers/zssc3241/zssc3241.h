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

#define ZSSC3241_CMD_FULL_MEASURE 0xAAu
#define ZSSC3241_FULL_RESPONSE_SIZE 7u
#define ZSSC3241_PRESSURE_RESPONSE_SIZE 4u
#define ZSSC3241_STATUS_POWERED (1u << 6)
#define ZSSC3241_STATUS_BUSY (1u << 5)
#define ZSSC3241_STATUS_MODE_MASK (3u << 3)
#define ZSSC3241_STATUS_MODE_RESERVED (3u << 3)
#define ZSSC3241_STATUS_MEMORY_ERROR (1u << 2)
#define ZSSC3241_STATUS_CONNECTION_FAULT (1u << 1)
#define ZSSC3241_STATUS_MATH_SATURATION (1u << 0)

typedef struct {
    uint32_t raw_u24;
    uint8_t status;
    ResultMetadata meta;
} ZsscRawPressureSample;

typedef struct {
    uint32_t generation;      /* Increment to invalidate pending completions. */
    uint64_t sample_sequence; /* Monotonic sample identity. */
    ZsscDriverState state;
    uint32_t active_correlation_id; /* Expected integration operation. */
    uint32_t active_transaction_id; /* Expected I2C bus transaction. */
    uint64_t        sample_monotonic_us;

    ZsscRawPressureSample raw_mailbox;
    bool raw_mailbox_valid;
    uint8_t command_buffer[1];
    uint8_t response_buffer[ZSSC3241_FULL_RESPONSE_SIZE];

    AppEventQueue *event_queue; /* Borrowed; owner must outlive the driver. */

    uint32_t sample_due_count;   /* Monotonic diagnostic counter. */
    uint32_t eoc_received_count; /* Monotonic diagnostic counter. */
    uint32_t raw_ready_count;    /* Monotonic diagnostic counter. */
    uint32_t timeout_count;      /* Monotonic diagnostic counter. */
    uint32_t error_count;        /* Monotonic diagnostic counter. */
} Zssc3241Driver;

void zssc3241_init(Zssc3241Driver *driver, AppEventQueue *event_queue);

void zssc3241_on_sample_due(Zssc3241Driver *driver, uint64_t now_us);

/* Builds the one-byte full-measure command and assigns identities for a new
 * attempt. The returned buffer remains driver-owned. */
bool zssc3241_prepare_start(Zssc3241Driver *driver,
                            uint32_t correlation_id,
                            const uint8_t **tx, uint16_t *tx_length);

void zssc3241_on_eoc_asserted(Zssc3241Driver *driver, uint64_t now_us);

/* Returns the driver-owned response buffer for the status + corrected sensor
 * value. Full-measure temperature bytes may follow and are ignored here. */
bool zssc3241_prepare_read(Zssc3241Driver *driver,
                           uint8_t **rx, uint16_t *rx_length);

void zssc3241_on_poll_due(Zssc3241Driver *driver, uint64_t now_us);

// Decodes status plus corrected U24 pressure into the driver-owned mailbox and
// publishes RAW_READY after a coherent response.
void zssc3241_on_i2c_completion(Zssc3241Driver *driver,
                                 uint32_t correlation_id,
                                 bool success,
                                 const uint8_t *rx_data,
                                 uint16_t rx_length,
                                 uint64_t now_us);

void zssc3241_on_timeout(Zssc3241Driver *driver, uint64_t now_us);

bool zssc3241_take_raw_pressure(Zssc3241Driver *driver,
                                ZsscRawPressureSample *sample_out);

bool zssc3241_status_is_production_usable(uint8_t status);

#endif
