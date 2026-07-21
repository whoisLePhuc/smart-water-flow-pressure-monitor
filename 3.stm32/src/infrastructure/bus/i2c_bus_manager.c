#include "i2c_bus_manager.h"

#include <string.h>

/**
 * @brief Finds a registered client by identifier.
 *
 * @return Matching client, or NULL if not found.
 */
static I2cBusClient *find_client(I2cBusManager *bus, uint32_t client_id)
{
    if (!bus)
        return NULL;

    for (uint8_t i = 0u; i < bus->client_count; ++i) {
        if (bus->clients[i].client_id == client_id)
            return &bus->clients[i];
    }

    return NULL;
}

/**
 * @brief Checks whether an address is allowed by a client mask.
 */
static bool client_accepts_address(const I2cBusClient *client, uint8_t address)
{
    return client &&
           (uint8_t)(address & client->address_mask) ==
               (uint8_t)(client->address_base & client->address_mask);
}

/**
 * @brief Delivers a terminal transaction result to its owner.
 */
static void notify(I2cBusManager *bus, const I2cPendingTransaction *transaction, I2cTransactionResult result)
{
    I2cBusClient *client = find_client(bus, transaction->client_id);
    if (!client || !client->on_complete)
        return;

    I2cTransactionCompletion completion = {
        .client_id = transaction->client_id,
        .transaction_id = transaction->transaction_id,
        .correlation_id = transaction->correlation_id,
        .client_generation = transaction->client_generation,
        .bus_generation = transaction->bus_generation,
        .result = result
    };

    client->on_complete(client->context, &completion);
}

/**
 * @brief Starts one admitted transaction on the physical port.
 */
static bool start_transaction(I2cBusManager *bus, const I2cPendingTransaction *transaction)
{
    I2cBusClient *client = find_client(bus, transaction->client_id);

    /* Reject stale clients and addresses outside the registered range. */
    if (!bus->port_bound || !client ||
        client->client_generation != transaction->client_generation ||
        !client_accepts_address(client, transaction->slave_address))
        return false;

    /*
     * Stamp the transaction with the current bus generation so callbacks from
     * an older bus state can be detected and ignored.
     */
    bus->active = *transaction;
    bus->active.bus_generation = bus->bus_generation;
    bus->busy = true;

    I2cPortRequest request = {
        .transaction_id = bus->active.transaction_id,
        .correlation_id = bus->active.correlation_id,
        .client_generation = bus->active.client_generation,
        .bus_generation = bus->active.bus_generation,
        .slave_address = bus->active.slave_address,
        .tx = bus->active.tx,
        .tx_length = bus->active.tx_len,
        .rx = bus->active.rx,
        .rx_length = bus->active.rx_len,
        .deadline_us = bus->active.deadline_us
    };

    if (bus->port.submit(bus->port.context, &request) != PORT_OK) {
        /* Roll back ownership when the physical port rejects submission. */
        bus->busy = false;
        memset(&bus->active, 0, sizeof(bus->active));
        return false;
    }

    return true;
}

/**
 * @brief Selects the highest-priority pending transaction.
 *
 * Lower priority values win. Equal priorities preserve FIFO order.
 */
static uint8_t best_pending_index(const I2cBusManager *bus)
{
    uint8_t best = 0u;

    for (uint8_t i = 1u; i < bus->pending_count; ++i) {
        if (bus->pending[i].priority < bus->pending[best].priority ||
            (bus->pending[i].priority == bus->pending[best].priority &&
             bus->pending[i].admission_sequence <
                 bus->pending[best].admission_sequence))
            best = i;
    }

    return best;
}

/**
 * @brief Removes one entry from the pending transaction array.
 */
static void remove_pending(I2cBusManager *bus, uint8_t index)
{
    for (uint8_t i = index; i + 1u < bus->pending_count; ++i)
        bus->pending[i] = bus->pending[i + 1u];

    if (bus->pending_count > 0u)
        bus->pending_count--;
}

/**
 * @brief Starts the next valid pending transaction.
 */
static void start_next(I2cBusManager *bus)
{
    while (!bus->busy && bus->pending_count > 0u) {
        uint8_t index = best_pending_index(bus);
        I2cPendingTransaction next = bus->pending[index];

        remove_pending(bus, index);

        if (start_transaction(bus, &next))
            return;

        /*
         * Invalid or stale queued requests are failed locally so they cannot
         * block later transactions.
         */
        bus->rejected_count++;
        notify(bus, &next, I2C_TRANSACTION_FAILED);
    }
}

void i2c_bus_init(I2cBusManager *bus, const I2cPort *port)
{
    if (!bus)
        return;

    memset(bus, 0, sizeof(*bus));

    /* Zero is reserved as an invalid generation or identifier. */
    bus->bus_generation = 1u;
    bus->next_admission_sequence = 1u;
    bus->next_transaction_id = 1u;

    if (i2c_port_is_valid(port)) {
        bus->port = *port;
        bus->port_bound = true;
    }
}

bool i2c_bus_bind_port(I2cBusManager *bus, const I2cPort *port)
{
    /*
     * Rebinding is allowed only while idle to avoid changing the physical
     * adapter beneath active or queued transactions.
     */
    if (!bus || !i2c_port_is_valid(port) || bus->busy ||
        bus->pending_count != 0u)
        return false;

    bus->port = *port;
    bus->port_bound = true;
    return true;
}

bool i2c_bus_register_client(I2cBusManager *bus,
                             const I2cBusClient *client)
{
    if (!bus || !client || client->client_id == 0u ||
        client->client_generation == 0u || client->address_mask == 0u ||
        !client->on_complete || bus->client_count >= I2C_BUS_MAX_CLIENTS ||
        find_client(bus, client->client_id))
        return false;

    bus->clients[bus->client_count++] = *client;
    return true;
}

bool i2c_bus_set_client_generation(I2cBusManager *bus,
                                   uint32_t client_id,
                                   uint32_t client_generation)
{
    I2cBusClient *client = find_client(bus, client_id);
    if (!client || client_generation == 0u)
        return false;

    /*
     * Existing transactions retain their original generation and will be
     * rejected as stale when started or completed.
     */
    client->client_generation = client_generation;
    return true;
}

I2cSubmitResult i2c_bus_submit(I2cBusManager *bus,
                               const I2cBusRequest *request,
                               uint32_t *transaction_id_out)
{
    if (!bus || !request || !transaction_id_out ||
        request->correlation_id == 0u || request->client_generation == 0u ||
        request->deadline_us == 0u ||
        (request->tx_length > 0u && !request->tx) ||
        (request->rx_length > 0u && !request->rx) ||
        (request->tx_length == 0u && request->rx_length == 0u))
        return I2C_SUBMIT_INVALID_PARAM;

    if (!bus->port_bound)
        return I2C_SUBMIT_NOT_READY;

    I2cBusClient *client = find_client(bus, request->client_id);
    if (!client)
        return I2C_SUBMIT_UNKNOWN_CLIENT;

    if (request->client_generation != client->client_generation)
        return I2C_SUBMIT_INVALID_PARAM;

    if (!client_accepts_address(client, request->slave_address))
        return I2C_SUBMIT_ADDRESS_REJECTED;

    /*
     * Skip zero after unsigned wraparound because zero is reserved as an
     * invalid transaction identifier.
     */
    uint32_t transaction_id = bus->next_transaction_id++;
    if (transaction_id == 0u)
        transaction_id = bus->next_transaction_id++;

    I2cPendingTransaction transaction = {
        .client_id = request->client_id,
        .transaction_id = transaction_id,
        .correlation_id = request->correlation_id,
        .client_generation = request->client_generation,
        .bus_generation = bus->bus_generation,
        .slave_address = request->slave_address,
        .tx = request->tx,
        .tx_len = request->tx_length,
        .rx = request->rx,
        .rx_len = request->rx_length,
        .deadline_us = request->deadline_us,
        .priority = request->priority,
        .admission_sequence = bus->next_admission_sequence++
    };

    if (!bus->busy) {
        if (!start_transaction(bus, &transaction)) {
            bus->rejected_count++;
            return I2C_SUBMIT_PORT_ERROR;
        }
    } else {
        if (bus->pending_count >= I2C_BUS_PENDING_CAPACITY) {
            bus->rejected_count++;
            return I2C_SUBMIT_NO_CAPACITY;
        }

        bus->pending[bus->pending_count++] = transaction;
        bus->arbitration_count++;
    }

    *transaction_id_out = transaction_id;
    return I2C_SUBMIT_ACCEPTED;
}

bool i2c_bus_complete(I2cBusManager *bus,
                      uint32_t transaction_id,
                      uint32_t correlation_id,
                      uint32_t client_generation,
                      uint32_t bus_generation,
                      I2cTransactionResult result)
{
    /*
     * Match every identity field before accepting a callback. This rejects
     * delayed completions from cancelled, timed-out, or recovered transfers.
     */
    if (!bus || !bus->busy || bus_generation != bus->bus_generation ||
        transaction_id != bus->active.transaction_id ||
        correlation_id != bus->active.correlation_id ||
        client_generation != bus->active.client_generation) {
        if (bus)
            bus->stale_completion_count++;
        return false;
    }

    I2cPendingTransaction completed = bus->active;

    /* Release the bus before invoking client code. */
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

/**
 * @brief Maps physical-port status to bus-manager transaction status.
 */
static I2cTransactionResult map_port_result(PortStatus result)
{
    if (result == PORT_OK)
        return I2C_TRANSACTION_OK;

    if (result == PORT_STATUS_TIMEOUT)
        return I2C_TRANSACTION_TIMEOUT;

    if (result == PORT_STATUS_BUSY)
        return I2C_TRANSACTION_BUSY;

    return I2C_TRANSACTION_FAILED;
}

bool i2c_bus_on_port_completion(I2cBusManager *bus,
                                const I2cPortRequest *request,
                                PortStatus result)
{
    if (!request) {
        if (bus)
            bus->stale_completion_count++;
        return false;
    }

    return i2c_bus_complete(bus, request->transaction_id,
                            request->correlation_id,
                            request->client_generation,
                            request->bus_generation,
                            map_port_result(result));
}

bool i2c_bus_tick(I2cBusManager *bus, uint64_t now_us)
{
    if (!bus || !bus->busy || now_us < bus->active.deadline_us)
        return false;

    I2cPendingTransaction timed_out = bus->active;

    if (bus->port.cancel)
        (void)bus->port.cancel(bus->port.context,
                               timed_out.transaction_id,
                               timed_out.bus_generation);

    /*
     * Incrementing the generation invalidates a late completion from the
     * cancelled physical transfer.
     */
    memset(&bus->active, 0, sizeof(bus->active));
    bus->busy = false;
    bus->bus_generation++;
    bus->timeout_count++;

    notify(bus, &timed_out, I2C_TRANSACTION_TIMEOUT);
    start_next(bus);
    return true;
}

uint8_t i2c_bus_cancel_client(I2cBusManager *bus, uint32_t client_id)
{
    if (!bus)
        return 0u;

    uint8_t cancelled = 0u;

    /* Remove all queued transactions owned by the client. */
    for (uint8_t i = 0u; i < bus->pending_count;) {
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

        if (bus->port.cancel)
            (void)bus->port.cancel(bus->port.context,
                                   active.transaction_id,
                                   active.bus_generation);

        /*
         * Advance the generation so a late physical callback cannot complete
         * the next transaction.
         */
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
    if (!bus)
        return;

    I2cPendingTransaction active = {0};
    bool had_active = bus->busy;

    if (had_active)
        active = bus->active;

    /*
     * The adapter receives the next generation so its recovery state matches
     * the manager after the operation completes.
     */
    if (bus->port.recover)
        (void)bus->port.recover(bus->port.context,
                                bus->bus_generation + 1u);

    bus->busy = false;
    memset(&bus->active, 0, sizeof(bus->active));
    bus->bus_generation++;
    bus->recovery_count++;

    if (had_active)
        notify(bus, &active, I2C_TRANSACTION_CANCELLED);

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
