#include "spi_bus_manager.h"
#include <string.h>

static SpiBusClient *find_client(SpiBusManager *bus, uint32_t client_id)
{
    for (uint8_t i = 0u; i < bus->client_count; ++i)
        if (bus->clients[i].client_id == client_id) return &bus->clients[i];
    return NULL;
}

static void notify(SpiBusManager *bus, const SpiPendingTransaction *txn,
                   SpiTransactionResult result)
{
    SpiBusClient *client = find_client(bus, txn->client_id);
    if (!client || !client->complete) return;
    SpiTransactionCompletion completion = {
        txn->client_id, txn->transaction_id, txn->correlation_id,
        txn->client_generation, bus->bus_generation, result
    };
    client->complete(client->context, &completion);
}

static bool start(SpiBusManager *bus, const SpiPendingTransaction *txn)
{
    SpiBusClient *client = find_client(bus, txn->client_id);
    if (!client || client->client_generation != txn->client_generation)
        return false;
    bus->active = *txn;
    bus->busy = true;
    if (!client->start(client->context, txn->chip_select, txn->tx, txn->rx,
                       txn->length, txn->transaction_id, txn->correlation_id,
                       txn->client_generation, bus->bus_generation,
                       txn->deadline_us)) {
        bus->busy = false;
        memset(&bus->active, 0, sizeof(bus->active));
        return false;
    }
    return true;
}

static uint8_t best(const SpiBusManager *bus)
{
    uint8_t selected = 0u;
    for (uint8_t i = 1u; i < bus->pending_count; ++i) {
        if (bus->pending[i].priority < bus->pending[selected].priority ||
            (bus->pending[i].priority == bus->pending[selected].priority &&
             bus->pending[i].admission_sequence <
                 bus->pending[selected].admission_sequence))
            selected = i;
    }
    return selected;
}

static void remove_pending(SpiBusManager *bus, uint8_t index)
{
    for (uint8_t i = index; i + 1u < bus->pending_count; ++i)
        bus->pending[i] = bus->pending[i + 1u];
    if (bus->pending_count > 0u) bus->pending_count--;
}

static void start_next(SpiBusManager *bus)
{
    while (!bus->busy && bus->pending_count > 0u) {
        uint8_t index = best(bus);
        SpiPendingTransaction txn = bus->pending[index];
        remove_pending(bus, index);
        if (start(bus, &txn)) return;
        bus->rejected_count++;
        notify(bus, &txn, SPI_TRANSACTION_FAILED);
    }
}

void spi_bus_init(SpiBusManager *bus)
{
    if (!bus) return;
    memset(bus, 0, sizeof(*bus));
    bus->bus_generation = 1u;
    bus->next_admission_sequence = 1u;
}

bool spi_bus_register_client(SpiBusManager *bus, const SpiBusClient *client)
{
    if (!bus || !client || client->client_id == 0u || !client->start ||
        bus->client_count >= SPI_BUS_MAX_CLIENTS ||
        find_client(bus, client->client_id))
        return false;
    bus->clients[bus->client_count++] = *client;
    return true;
}

static bool id_exists(const SpiBusManager *bus, uint32_t id)
{
    if (bus->busy && bus->active.transaction_id == id) return true;
    for (uint8_t i = 0u; i < bus->pending_count; ++i)
        if (bus->pending[i].transaction_id == id) return true;
    return false;
}

bool spi_bus_submit(SpiBusManager *bus, uint32_t client_id,
                    uint32_t transaction_id, uint32_t correlation_id,
                    const uint8_t *tx, uint8_t *rx, uint16_t length,
                    uint64_t deadline_us, uint8_t priority)
{
    SpiBusClient *client;
    if (!bus || transaction_id == 0u || length == 0u || !tx || !rx ||
        deadline_us == 0u || id_exists(bus, transaction_id) ||
        !(client = find_client(bus, client_id)))
        return false;
    SpiPendingTransaction txn = {
        client_id, transaction_id, correlation_id,
        client->client_generation, client->chip_select, tx, rx, length,
        deadline_us, priority, bus->next_admission_sequence++
    };
    if (!bus->busy) {
        if (start(bus, &txn)) return true;
        bus->rejected_count++;
        return false;
    }
    if (bus->pending_count >= SPI_BUS_PENDING_CAPACITY) {
        bus->rejected_count++;
        return false;
    }
    bus->pending[bus->pending_count++] = txn;
    return true;
}

bool spi_bus_complete(SpiBusManager *bus, uint32_t transaction_id,
                      uint32_t correlation_id, uint32_t client_generation,
                      uint32_t bus_generation, SpiTransactionResult result)
{
    if (!bus || !bus->busy || bus_generation != bus->bus_generation ||
        transaction_id != bus->active.transaction_id ||
        correlation_id != bus->active.correlation_id ||
        client_generation != bus->active.client_generation) {
        if (bus) bus->stale_completion_count++;
        return false;
    }
    SpiPendingTransaction completed = bus->active;
    memset(&bus->active, 0, sizeof(bus->active));
    bus->busy = false;
    if (result == SPI_TRANSACTION_OK) bus->completed_count++;
    if (result == SPI_TRANSACTION_TIMEOUT) bus->timeout_count++;
    notify(bus, &completed, result);
    start_next(bus);
    return true;
}

bool spi_bus_tick(SpiBusManager *bus, uint64_t now_us)
{
    if (!bus || !bus->busy || now_us < bus->active.deadline_us) return false;
    return spi_bus_complete(bus, bus->active.transaction_id,
                            bus->active.correlation_id,
                            bus->active.client_generation,
                            bus->bus_generation, SPI_TRANSACTION_TIMEOUT);
}

void spi_bus_recover(SpiBusManager *bus)
{
    if (!bus) return;
    if (bus->busy) {
        SpiPendingTransaction active = bus->active;
        bus->busy = false;
        memset(&bus->active, 0, sizeof(bus->active));
        bus->bus_generation++;
        notify(bus, &active, SPI_TRANSACTION_CANCELLED);
    } else {
        bus->bus_generation++;
    }
    start_next(bus);
}

const SpiPendingTransaction *spi_bus_active(const SpiBusManager *bus)
{
    return bus && bus->busy ? &bus->active : NULL;
}
