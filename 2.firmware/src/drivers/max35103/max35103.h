#ifndef SWFPM_MAX35103_H
#define SWFPM_MAX35103_H

#include <stdint.h>
#include <stdbool.h>
#include "domain/common/metadata.h"
#include "infrastructure/event/event_id.h"
#include "infrastructure/queues/app_event_queue.h"

/*
 * MAX35103 portable driver.
 *
 * Boundary:
 *   - Receives EVT_MAX_IRQ_ASSERTED from INT adapter
 *   - Consumes correlated SPI completion events supplied by the integration
 *   - Publishes EVT_MAX_RAW_READY after the current skeleton accepts success
 *
 * Does NOT:
 *   - Submit SPI operations; that integration is not implemented here yet
 *   - Parse or retain a coherent MAX result payload
 *   - Compute flow/temperature (delegated to calibration service)
 *   - Access persistent storage
 */

typedef enum {
    MAX_STATE_IDLE,
    MAX_STATE_IRQ_RECEIVED,
    MAX_STATE_SPI_READ_STATUS,
    MAX_STATE_SPI_READ_RESULT,
    MAX_STATE_VALIDATING,
    MAX_STATE_RAW_READY,
    MAX_STATE_TIMEOUT,
    MAX_STATE_RECOVERY
} MaxDriverState;

typedef struct {
    uint32_t generation;      /* Increment to invalidate pending completions. */
    uint64_t sample_sequence; /* Monotonic sample identity. */
    MaxDriverState  state;
    uint32_t active_correlation_id; /* Completion must match the active operation. */
    uint64_t        sample_monotonic_us;

    AppEventQueue *event_queue; /* Borrowed; owner must outlive the driver. */
    uint8_t spi_cs_gpio;
    uint32_t supervision_timeout_us;

    uint32_t irq_received_count;   /* Monotonic diagnostic counter. */
    uint32_t spi_completion_count; /* Monotonic diagnostic counter. */
    uint32_t raw_ready_count;      /* Monotonic diagnostic counter. */
    uint32_t timeout_count;        /* Monotonic diagnostic counter. */
    uint32_t error_count;          /* Monotonic diagnostic counter. */
} Max35103Driver;

void max35103_init(Max35103Driver *driver, AppEventQueue *event_queue);

// Records the start timestamp for a new measurement attempt. The integration
// layer remains responsible for starting the correlated SPI operation.
void max35103_on_irq(Max35103Driver *driver, uint64_t now_us);

// Consumes the SPI buffer only for the duration of the call. The current
// skeleton does not parse rx_data; callers must not treat RAW_READY as a
// coherent production result until that integration is implemented.
void max35103_on_spi_completion(Max35103Driver *driver,
                                uint32_t correlation_id,
                                bool success,
                                const uint8_t *rx_data,
                                uint16_t rx_length,
                                uint64_t now_us);

// Terminates the active attempt locally; it does not reset the peripheral or
// the system FSM.
void max35103_on_timeout(Max35103Driver *driver, uint64_t now_us);

#endif
