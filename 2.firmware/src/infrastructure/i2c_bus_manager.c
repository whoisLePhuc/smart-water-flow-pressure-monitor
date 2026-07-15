#include "infrastructure/i2c_bus_manager.h"
#include <string.h>

void i2c_bus_init(I2cBusManager *bus)
{
    memset(bus, 0, sizeof(*bus));
    bus->bus_generation = 1;
}

bool i2c_bus_register_client(I2cBusManager *bus, const I2cBusClient *client)
{
    if (!bus || !client) return false;
    if (bus->client_count >= I2C_BUS_MAX_CLIENTS) return false;

    bus->clients[bus->client_count] = *client;
    bus->client_count++;
    return true;
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
    (void)transaction_id;
    (void)priority;

    if (!bus) return false;

    /* Find client */
    I2cBusClient *client = NULL;
    for (uint8_t i = 0; i < bus->client_count; i++) {
        if (bus->clients[i].client_id == client_id) {
            client = &bus->clients[i];
            break;
        }
    }
    if (!client) return false;

    /* Bus busy — reject (priority-based arbitration will be implemented in later phase) */
    if (bus->busy) {
        bus->arbitration_count++;
        return false;
    }

    bus->busy = true;
    bus->active_client_id = client_id;

    bool ok = client->submit_tx(
        client->context,
        client->slave_address,
        tx, tx_len,
        rx, rx_len,
        correlation_id,
        deadline_us);

    if (!ok) {
        bus->busy = false;
        return false;
    }

    return true;
}

void i2c_bus_recover(I2cBusManager *bus)
{
    if (!bus) return;
    bus->bus_generation++;
    bus->busy = false;
    bus->recovery_count++;
}
