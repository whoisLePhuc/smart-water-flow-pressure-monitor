#include "platform/providers/linux_i2c_provider.h"
#include <string.h>

void linux_i2c_init(LinuxI2cProvider *provider,
                    LinuxScheduledActionQueue *action_queue)
{
    memset(provider, 0, sizeof(*provider));
    provider->action_queue = action_queue;
    provider->resource_generation = 1;
}

bool linux_i2c_register_peer(LinuxI2cProvider *provider,
                             uint8_t address, LinuxI2cPeer peer)
{
    if (!provider) return false;

    /* Check for duplicate address */
    for (uint8_t i = 0; i < provider->peer_count; i++) {
        if (provider->peers[i].address == address)
            return false;  /* Duplicate address rejected */
    }

    if (provider->peer_count >= LINUX_I2C_MAX_PEERS)
        return false;

    provider->peers[provider->peer_count].address = address;
    provider->peers[provider->peer_count].peer = peer;
    provider->peer_count++;
    return true;
}

bool linux_i2c_submit(LinuxI2cProvider *provider,
                      const LinuxI2cRequest *request)
{
    if (!provider || !request) return false;

    /* Find peer by address */
    LinuxI2cPeer *target = NULL;
    for (uint8_t i = 0; i < provider->peer_count; i++) {
        if (provider->peers[i].address == request->slave_address) {
            target = &provider->peers[i].peer;
            break;
        }
    }

    if (!target || !target->i2c_plan) {
        provider->admission_rejected++;
        return false;
    }

    if (provider->active) {
        provider->admission_rejected++;
        return false;
    }

    uint64_t latency_us = 0;
    uint32_t status_flags = 0;

    bool accepted = target->i2c_plan(
        target->context,
        request->slave_address,
        request->tx_data, request->tx_length,
        request->rx_data, request->rx_length,
        &latency_us, &status_flags);

    if (!accepted) {
        provider->admission_rejected++;
        return false;
    }

    provider->active = true;
    provider->active_op_id = request->operation_id;
    provider->admission_accepted++;

    LinuxScheduledAction action;
    memset(&action, 0, sizeof(action));
    action.due_us = request->deadline_us + latency_us;
    action.action_class = ACTION_CLASS_I2C_COMPLETION;
    action.resource_id = request->slave_address;
    action.resource_generation = provider->resource_generation;
    action.operation_id = request->operation_id;
    action.correlation_id = request->correlation_id;
    action.owner_generation = request->owner_generation;
    action.detail_flags = status_flags;

    return action_queue_schedule(provider->action_queue, &action);
}

bool linux_i2c_cancel(LinuxI2cProvider *provider,
                      uint32_t operation_id,
                      uint32_t expected_generation)
{
    if (!provider) return false;
    bool cancelled = action_queue_cancel(
        provider->action_queue, operation_id, expected_generation);
    if (cancelled && provider->active && provider->active_op_id == operation_id)
        provider->active = false;
    return cancelled;
}

void linux_i2c_recover(LinuxI2cProvider *provider)
{
    if (!provider) return;
    provider->resource_generation++;
    provider->active = false;
}
