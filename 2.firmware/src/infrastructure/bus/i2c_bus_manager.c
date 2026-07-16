#include "i2c_bus_manager.h"
#include <string.h>

static I2cBusClient *find_client(I2cBusManager *bus, uint32_t client_id)
{
    for (uint8_t i = 0; i < bus->client_count; ++i) {
        if (bus->clients[i].client_id == client_id)
            return &bus->clients[i];
    }
    return NULL;
}

static void notify(I2cBusManager *bus,
                   const I2cPendingTransaction *transaction,
                   I2cTransactionResult result)
{
    I2cBusClient *client = find_client(bus, transaction->client_id);
    if (!client || !client->on_complete)
        return;
    I2cTransactionCompletion completion = {
        .client_id = transaction->client_id,
        .transaction_id = transaction->transaction_id,
        .correlation_id = transaction->correlation_id,
        .client_generation = transaction->client_generation,
        .bus_generation = bus->bus_generation,
        .result = result
    };
    client->on_complete(client->context, &completion);
}

static bool start_transaction(I2cBusManager *bus,
                              const I2cPendingTransaction *transaction)
{
    I2cBusClient *client = find_client(bus, transaction->client_id);
    if (!client || !client->submit_tx ||
        client->client_generation != transaction->client_generation)
        return false;

    bus->active = *transaction;
    bus->busy = true;
    if (!client->submit_tx(client->context, transaction->slave_address,
                           transaction->tx, transaction->tx_len,
                           transaction->rx, transaction->rx_len,
                           transaction->correlation_id,
                           transaction->deadline_us)) {
        bus->busy = false;
        memset(&bus->active, 0, sizeof(bus->active));
        return false;
    }
    return true;
}

static uint8_t best_pending_index(const I2cBusManager *bus)
{
    uint8_t best = 0;
    for (uint8_t i = 1; i < bus->pending_count; ++i) {
        if (bus->pending[i].priority < bus->pending[best].priority ||
            (bus->pending[i].priority == bus->pending[best].priority &&
             bus->pending[i].admission_sequence <
                 bus->pending[best].admission_sequence))
            best = i;
    }
    return best;
}

static void remove_pending(I2cBusManager *bus, uint8_t index)
{
    for (uint8_t i = index; i + 1u < bus->pending_count; ++i)
        bus->pending[i] = bus->pending[i + 1u];
    if (bus->pending_count > 0u)
        bus->pending_count--;
}

static void start_next(I2cBusManager *bus)
{
    while (!bus->busy && bus->pending_count > 0u) {
        uint8_t index = best_pending_index(bus);
        I2cPendingTransaction next = bus->pending[index];
        remove_pending(bus, index);
        if (start_transaction(bus, &next))
            return;
        bus->rejected_count++;
        notify(bus, &next, I2C_TRANSACTION_FAILED);
    }
}

void i2c_bus_init(I2cBusManager *bus)
{
    if (!bus) return;
    memset(bus, 0, sizeof(*bus));
    bus->bus_generation = 1u;
    bus->next_admission_sequence = 1u;
}

bool i2c_bus_register_client(I2cBusManager *bus, const I2cBusClient *client)
{
    if (!bus || !client || client->client_id == 0u || !client->submit_tx ||
        bus->client_count >= I2C_BUS_MAX_CLIENTS)
        return false;
    if (find_client(bus, client->client_id))
        return false;
    bus->clients[bus->client_count++] = *client;
    return true;
}

static bool transaction_id_exists(const I2cBusManager *bus,
                                  uint32_t transaction_id)
{
    if (bus->busy && bus->active.transaction_id == transaction_id)
        return true;
    for (uint8_t i = 0; i < bus->pending_count; ++i) {
        if (bus->pending[i].transaction_id == transaction_id)
            return true;
    }
    return false;
}

bool i2c_bus_submit(I2cBusManager *bus,
                    uint32_t client_id,
                    uint32_t transaction_id,
                    uint32_t correlation_id,
                    const uint8_t *tx, uint16_t tx_len,
                    uint8_t *rx, uint16_t rx_len,
                    uint64_t deadline_us,
                    uint8_t priority)
{
    if (!bus || transaction_id == 0u || deadline_us == 0u ||
        (tx_len > 0u && !tx) || (rx_len > 0u && !rx) ||
        transaction_id_exists(bus, transaction_id))
        return false;
    I2cBusClient *client = find_client(bus, client_id);
    if (!client) return false;

    I2cPendingTransaction transaction = {
        .client_id = client_id,
        .transaction_id = transaction_id,
        .correlation_id = correlation_id,
        .client_generation = client->client_generation,
        .slave_address = client->slave_address,
        .tx = tx,
        .tx_len = tx_len,
        .rx = rx,
        .rx_len = rx_len,
        .deadline_us = deadline_us,
        .priority = priority,
        .admission_sequence = bus->next_admission_sequence++
    };

    if (!bus->busy) {
        if (start_transaction(bus, &transaction))
            return true;
        bus->rejected_count++;
        return false;
    }
    if (bus->pending_count >= I2C_BUS_PENDING_CAPACITY) {
        bus->rejected_count++;
        return false;
    }
    bus->pending[bus->pending_count++] = transaction;
    bus->arbitration_count++;
    return true;
}

bool i2c_bus_complete(I2cBusManager *bus,
                      uint32_t transaction_id,
                      uint32_t correlation_id,
                      uint32_t client_generation,
                      uint32_t bus_generation,
                      I2cTransactionResult result)
{
    if (!bus || !bus->busy || bus_generation != bus->bus_generation ||
        transaction_id != bus->active.transaction_id ||
        correlation_id != bus->active.correlation_id ||
        client_generation != bus->active.client_generation) {
        if (bus) bus->stale_completion_count++;
        return false;
    }
    I2cPendingTransaction completed = bus->active;
    memset(&bus->active, 0, sizeof(bus->active));
    bus->busy = false;
    if (result == I2C_TRANSACTION_TIMEOUT)
        bus->timeout_count++;
    else if (result == I2C_TRANSACTION_OK)
        bus->completed_count++;
    notify(bus, &completed, result);
    start_next(bus);
    return true;
}

bool i2c_bus_tick(I2cBusManager *bus, uint64_t now_us)
{
    if (!bus || !bus->busy || now_us < bus->active.deadline_us)
        return false;
    return i2c_bus_complete(bus, bus->active.transaction_id,
                            bus->active.correlation_id,
                            bus->active.client_generation,
                            bus->bus_generation,
                            I2C_TRANSACTION_TIMEOUT);
}

uint8_t i2c_bus_cancel_client(I2cBusManager *bus, uint32_t client_id)
{
    if (!bus) return 0u;
    uint8_t cancelled = 0u;
    for (uint8_t i = 0; i < bus->pending_count;) {
        if (bus->pending[i].client_id == client_id) {
            I2cPendingTransaction transaction = bus->pending[i];
            remove_pending(bus, i);
            notify(bus, &transaction, I2C_TRANSACTION_CANCELLED);
            cancelled++;
        } else {
            i++;
        }
    }
    if (bus->busy && bus->active.client_id == client_id) {
        I2cPendingTransaction active = bus->active;
        bus->busy = false;
        memset(&bus->active, 0, sizeof(bus->active));
        bus->bus_generation++;
        notify(bus, &active, I2C_TRANSACTION_CANCELLED);
        cancelled++;
        start_next(bus);
    }
    return cancelled;
}

void i2c_bus_recover(I2cBusManager *bus)
{
    if (!bus) return;
    if (bus->busy) {
        I2cPendingTransaction active = bus->active;
        bus->busy = false;
        memset(&bus->active, 0, sizeof(bus->active));
        bus->bus_generation++;
        notify(bus, &active, I2C_TRANSACTION_CANCELLED);
    } else {
        bus->bus_generation++;
    }
    bus->recovery_count++;
    start_next(bus);
}

uint8_t i2c_bus_pending_count(const I2cBusManager *bus)
{
    return bus ? bus->pending_count : 0u;
}

const I2cPendingTransaction *i2c_bus_active(const I2cBusManager *bus)
{
    return bus && bus->busy ? &bus->active : NULL;
}
