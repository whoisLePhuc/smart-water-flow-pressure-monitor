#include "providers/linux_spi_provider.h"
#include <string.h>

void linux_spi_init(LinuxSpiProvider *provider,
                    LinuxScheduledActionQueue *action_queue)
{
    memset(provider, 0, sizeof(*provider));
    provider->action_queue = action_queue;
    provider->resource_generation = 1;
}

bool linux_spi_register_peer(LinuxSpiProvider *provider, LinuxSpiPeer peer)
{
    if (!provider) return false;
    provider->peer = peer;
    return true;
}

bool linux_spi_submit(LinuxSpiProvider *provider,
                      const LinuxSpiRequest *request)
{
    if (!provider || !request) return false;

    /* Must have a peer registered */
    if (!provider->peer.spi_plan) {
        provider->admission_rejected++;
        return false;
    }

    /* Only one active operation at a time (measurement baseline) */
    if (provider->active) {
        provider->admission_rejected++;
        return false;
    }

    /* Ask peer to plan the transfer */
    uint64_t latency_us = 0;
    uint32_t status_flags = 0;

    bool accepted = provider->peer.spi_plan(
        provider->peer.context,
        request->tx_data,
        request->rx_data,
        request->length,
        &latency_us,
        &status_flags);

    if (!accepted) {
        provider->admission_rejected++;
        return false;
    }

    provider->active = true;
    provider->active_operation_id = request->operation_id;
    provider->admission_accepted++;

    /* Schedule terminal completion via action queue */
    LinuxScheduledAction action;
    memset(&action, 0, sizeof(action));
    action.due_us = request->deadline_us + latency_us;
    action.action_class = ACTION_CLASS_SPI_COMPLETION;
    action.resource_id = 0;
    action.resource_generation = provider->resource_generation;
    action.operation_id = request->operation_id;
    action.correlation_id = request->correlation_id;
    action.owner_generation = request->owner_generation;
    action.detail_flags = status_flags;
    action.payload_size = 0;

    return action_queue_schedule(provider->action_queue, &action);
}

bool linux_spi_cancel(LinuxSpiProvider *provider,
                      uint32_t operation_id,
                      uint32_t expected_generation)
{
    if (!provider) return false;

    bool cancelled = action_queue_cancel(
        provider->action_queue, operation_id, expected_generation);

    if (cancelled && provider->active &&
        provider->active_operation_id == operation_id) {
        provider->active = false;
    }

    return cancelled;
}

void linux_spi_recover(LinuxSpiProvider *provider)
{
    if (!provider) return;
    provider->resource_generation++;
    provider->active = false;
}
