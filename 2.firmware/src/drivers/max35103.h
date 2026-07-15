#ifndef SWFPM_MAX35103_H
#define SWFPM_MAX35103_H

#include <stdint.h>
#include <stdbool.h>
#include "event/data_model.h"
#include "event/app_event_queue.h"

/*
 * MAX35103 portable driver.
 *
 * Boundary:
 *   - Receives EVT_MAX_IRQ_ASSERTED from INT adapter
 *   - Submits correlated SPI operations
 *   - Publishes EVT_MAX_SPI_COMPLETED/FAILED via event queue
 *   - Publishes EVT_MAX_RAW_READY when coherent mailbox is ready
 *
 * Does NOT:
 *   - Configure hardware directly (done via SPI port)
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
    uint32_t        generation;
    uint64_t        sample_sequence;
    MaxDriverState  state;
    uint32_t        active_correlation_id;
    uint64_t        sample_monotonic_us;

    /* Event queue for posting events */
    AppEventQueue  *event_queue;

    /* Configuration */
    uint8_t spi_cs_gpio;
    uint32_t supervision_timeout_us;

    /* Diagnostics */
    uint32_t irq_received_count;
    uint32_t spi_completion_count;
    uint32_t raw_ready_count;
    uint32_t timeout_count;
    uint32_t error_count;
} Max35103Driver;

void max35103_init(Max35103Driver *driver, AppEventQueue *event_queue);

/* Called when EVT_MAX_IRQ_ASSERTED is received */
void max35103_on_irq(Max35103Driver *driver, uint64_t now_us);

/* Called when EVT_MAX_SPI_COMPLETED/FAILED is received.
 * rx_data points to the SPI RX buffer. */
void max35103_on_spi_completion(Max35103Driver *driver,
                                uint32_t correlation_id,
                                bool success,
                                const uint8_t *rx_data,
                                uint16_t rx_length,
                                uint64_t now_us);

/* Called when EVT_MAX_RESULT_TIMEOUT fires */
void max35103_on_timeout(Max35103Driver *driver, uint64_t now_us);

#endif
