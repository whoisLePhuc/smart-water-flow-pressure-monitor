#include "i2c_bus_manager.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    I2cPortRequest active_request;
    uint32_t submissions;
    uint32_t cancellations;
    bool active;
} FakePort;

typedef struct {
    I2cTransactionCompletion completions[8];
    uint8_t completion_count;
} FakeClient;

static PortStatus port_submit(void *context, const I2cPortRequest *request)
{
    FakePort *port = context;
    if (port->active)
        return PORT_STATUS_BUSY;
    port->active_request = *request;
    port->active = true;
    port->submissions++;
    return PORT_OK;
}

static PortStatus port_cancel(void *context, uint32_t transaction_id,
                              uint32_t bus_generation)
{
    FakePort *port = context;
    if (!port->active ||
        port->active_request.transaction_id != transaction_id ||
        port->active_request.bus_generation != bus_generation)
        return PORT_STATUS_INVALID_PARAM;
    port->active = false;
    port->cancellations++;
    return PORT_OK;
}

static PortStatus port_recover(void *context, uint32_t new_bus_generation)
{
    FakePort *port = context;
    (void)new_bus_generation;
    port->active = false;
    return PORT_OK;
}

static void client_complete(
    void *context,
    const I2cTransactionCompletion *completion)
{
    FakeClient *client = context;
    client->completions[client->completion_count++] = *completion;
}

static uint32_t submit_request(I2cBusManager *bus,
                               uint32_t client_id,
                               uint32_t generation,
                               uint8_t address,
                               uint32_t correlation_id,
                               uint8_t priority,
                               uint64_t deadline_us)
{
    static uint8_t byte;
    I2cBusRequest request = {
        .client_id = client_id,
        .correlation_id = correlation_id,
        .client_generation = generation,
        .slave_address = address,
        .tx = &byte,
        .tx_length = 1u,
        .deadline_us = deadline_us,
        .priority = priority
    };
    uint32_t transaction_id = 0u;
    assert(i2c_bus_submit(bus, &request, &transaction_id) ==
           I2C_SUBMIT_ACCEPTED);
    return transaction_id;
}

int main(void)
{
    FakePort physical;
    FakeClient pressure;
    FakeClient storage;
    memset(&physical, 0, sizeof(physical));
    memset(&pressure, 0, sizeof(pressure));
    memset(&storage, 0, sizeof(storage));

    I2cPort port = {
        .context = &physical,
        .submit = port_submit,
        .cancel = port_cancel,
        .recover = port_recover
    };
    I2cBusManager bus;
    i2c_bus_init(&bus, &port);

    I2cBusClient pressure_client = {
        .client_id = 1u,
        .client_generation = 3u,
        .address_base = 0x28u,
        .address_mask = 0x7Fu,
        .context = &pressure,
        .on_complete = client_complete
    };
    I2cBusClient storage_client = {
        .client_id = 2u,
        .client_generation = 1u,
        .address_base = 0x50u,
        .address_mask = 0x7Eu,
        .context = &storage,
        .on_complete = client_complete
    };
    assert(i2c_bus_register_client(&bus, &pressure_client));
    assert(i2c_bus_register_client(&bus, &storage_client));
    assert(!i2c_bus_register_client(&bus, &storage_client));

    uint32_t first = submit_request(&bus, 2u, 1u, 0x50u, 100u, 4u,
                                    1000u);
    uint32_t second = submit_request(&bus, 2u, 1u, 0x51u, 101u, 4u,
                                     1100u);
    uint32_t pressure_id = submit_request(&bus, 1u, 3u, 0x28u, 102u, 1u,
                                          1200u);
    assert(first != second && second != pressure_id);
    assert(i2c_bus_pending_count(&bus) == 2u);

    I2cPortRequest completed = physical.active_request;
    physical.active = false;
    assert(!i2c_bus_complete(&bus, first, 999u, 1u, 1u,
                             I2C_TRANSACTION_OK));
    assert(i2c_bus_on_port_completion(&bus, &completed, PORT_OK));
    assert(i2c_bus_active(&bus)->transaction_id == pressure_id);
    assert(storage.completion_count == 1u);

    assert(i2c_bus_tick(&bus, 1300u));
    assert(pressure.completions[0].result == I2C_TRANSACTION_TIMEOUT);
    assert(i2c_bus_active(&bus)->transaction_id == second);
    assert(physical.cancellations == 1u);
    assert(i2c_bus_cancel_client(&bus, 2u) == 1u);
    assert(!bus.busy);
    assert(bus.bus_generation == 3u);

    I2cBusRequest invalid_address = {
        .client_id = 2u,
        .correlation_id = 200u,
        .client_generation = 1u,
        .slave_address = 0x52u,
        .tx = (const uint8_t *)"x",
        .tx_length = 1u,
        .deadline_us = 2000u
    };
    uint32_t ignored = 0u;
    assert(i2c_bus_submit(&bus, &invalid_address, &ignored) ==
           I2C_SUBMIT_ADDRESS_REJECTED);

    puts("I2C Bus Manager Tests: PASS");
    return 0;
}
