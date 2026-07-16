#ifndef SWFPM_I2C_BUS_MANAGER_H
#define SWFPM_I2C_BUS_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "infrastructure/queues/app_event_queue.h"

/*
 * I2cBusManager — single owner của physical I2C bus.
 *
 * ZSSC3241 và F-RAM cùng đi qua manager này.
 * Manager chịu trách nhiệm arbitration, timeout, cancellation và recovery.
 * Priority: pressure measurement > background storage.
 */

#define I2C_BUS_MAX_CLIENTS 4

typedef enum {
    I2C_TRANSACTION_OK,
    I2C_TRANSACTION_BUSY,
    I2C_TRANSACTION_TIMEOUT,
    I2C_TRANSACTION_FAILED,
    I2C_TRANSACTION_CANCELLED
} I2cTransactionResult;

typedef struct {
    uint32_t client_id;
    uint32_t transaction_id;
    uint32_t correlation_id;
    uint32_t client_generation;
    uint32_t bus_generation;
    I2cTransactionResult result;
} I2cTransactionCompletion;

/* Client interface — physical I2C provider callback */
typedef struct I2cBusClient {
    uint32_t client_id;
    uint8_t  slave_address;
    uint32_t client_generation;
    void    *context;
    bool (*submit_tx)(void *ctx, uint8_t addr,
                      const uint8_t *tx, uint16_t tx_len,
                      uint8_t *rx, uint16_t rx_len,
                      uint32_t correlation_id,
                      uint64_t deadline_us);
} I2cBusClient;

typedef struct {
    I2cBusClient clients[I2C_BUS_MAX_CLIENTS];
    uint8_t      client_count;
    bool         busy;
    uint32_t     active_client_id;
    uint32_t     bus_generation;
    uint32_t     arbitration_count;
    uint32_t     timeout_count;
    uint32_t     recovery_count;
} I2cBusManager;

void i2c_bus_init(I2cBusManager *bus);

bool i2c_bus_register_client(I2cBusManager *bus, const I2cBusClient *client);

/* Submit transaction through the bus manager.
 * Priority: higher priority_value = lower number = processed first.
 * Returns true if queued/accepted. */
bool i2c_bus_submit(I2cBusManager *bus,
                    uint32_t client_id,
                    uint32_t transaction_id,
                    uint32_t correlation_id,
                    const uint8_t *tx, uint16_t tx_len,
                    uint8_t *rx, uint16_t rx_len,
                    uint64_t deadline_us,
                    uint8_t priority);

/* Bus recovery — increments bus_generation, invalidates stale completions */
void i2c_bus_recover(I2cBusManager *bus);

#endif
