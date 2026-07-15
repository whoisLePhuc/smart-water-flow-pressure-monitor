#ifndef SWFPM_LINUX_SPI_PROVIDER_H
#define SWFPM_LINUX_SPI_PROVIDER_H

#include <stdint.h>
#include <stdbool.h>
#include "platform/linux_scheduled_action_queue.h"

/* ── SPI Request ─────────────────────────────────────────── */

typedef struct {
    uint32_t operation_id;
    uint32_t correlation_id;
    uint32_t owner_generation;
    uint64_t deadline_us;
    const uint8_t *tx_data;
    uint8_t *rx_data;
    uint16_t length;
    uint32_t config_id;
} LinuxSpiRequest;

/* ── Peer interface ──────────────────────────────────────── */

typedef struct {
    /* Plan an SPI transfer. Returns true if accepted.
     * Sets latency_us to the simulated transfer duration.
     * Sets status to the planned completion status. */
    bool (*spi_plan)(void *context,
                     const uint8_t *tx, uint8_t *rx,
                     uint16_t length,
                     uint64_t *latency_us,
                     uint32_t *status_flags);
    void *context;
} LinuxSpiPeer;

/* ── Provider ────────────────────────────────────────────── */

typedef struct {
    LinuxScheduledActionQueue *action_queue;
    LinuxSpiPeer               peer;
    uint32_t                   resource_generation;
    uint32_t                   active_operation_id;
    bool                       active;
    uint32_t                   admission_accepted;
    uint32_t                   admission_rejected;
    uint32_t                   completion_count;
} LinuxSpiProvider;

/* ── API ──────────────────────────────────────────────────── */

void linux_spi_init(LinuxSpiProvider *provider,
                    LinuxScheduledActionQueue *action_queue);

bool linux_spi_register_peer(LinuxSpiProvider *provider,
                             LinuxSpiPeer peer);

/* Submit an async SPI request. Returns true if accepted.
 * Completion is scheduled via action_queue. */
bool linux_spi_submit(LinuxSpiProvider *provider,
                      const LinuxSpiRequest *request);

/* Cancel active operation by generation. Returns true if cancelled. */
bool linux_spi_cancel(LinuxSpiProvider *provider,
                      uint32_t operation_id,
                      uint32_t expected_generation);

/* Increment resource generation — invalidates pending completions. */
void linux_spi_recover(LinuxSpiProvider *provider);

#endif /* SWFPM_LINUX_SPI_PROVIDER_H */
