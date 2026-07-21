#ifndef SWFPM_I2C_BUS_MANAGER_H
#define SWFPM_I2C_BUS_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "ports/i2c_port.h"

/* One portable owner for the physical I2C bus shared by ZSSC3241 and F-RAM.
 * Lower numeric priority wins; an admitted transaction is never pre-empted. */

#define I2C_BUS_MAX_CLIENTS 4u
#define I2C_BUS_PENDING_CAPACITY 8u

typedef enum {
    I2C_TRANSACTION_OK,
    I2C_TRANSACTION_BUSY,
    I2C_TRANSACTION_TIMEOUT,
    I2C_TRANSACTION_FAILED,
    I2C_TRANSACTION_CANCELLED,
    I2C_TRANSACTION_STALE
} I2cTransactionResult;

typedef enum {
    I2C_SUBMIT_ACCEPTED = 0,
    I2C_SUBMIT_INVALID_PARAM,
    I2C_SUBMIT_NOT_READY,
    I2C_SUBMIT_UNKNOWN_CLIENT,
    I2C_SUBMIT_ADDRESS_REJECTED,
    I2C_SUBMIT_NO_CAPACITY,
    I2C_SUBMIT_PORT_ERROR
} I2cSubmitResult;

typedef struct {
    uint32_t client_id;
    uint32_t transaction_id;
    uint32_t correlation_id;
    uint32_t client_generation;
    uint32_t bus_generation;
    I2cTransactionResult result;
} I2cTransactionCompletion;

typedef struct I2cBusClient {
    uint32_t client_id;
    uint32_t client_generation;
    uint8_t address_base;
    uint8_t address_mask;
    void *context; /* Borrowed; client owner must outlive the manager. */
    void (*on_complete)(void *ctx,
                        const I2cTransactionCompletion *completion);
} I2cBusClient;

typedef struct {
    uint32_t client_id;
    uint32_t correlation_id;
    uint32_t client_generation;
    uint8_t slave_address;
    const uint8_t *tx;
    uint16_t tx_length;
    uint8_t *rx;
    uint16_t rx_length;
    uint64_t deadline_us;
    uint8_t priority;
} I2cBusRequest;

typedef struct {
    uint32_t client_id;
    uint32_t transaction_id;
    uint32_t correlation_id;
    uint32_t client_generation;
    uint32_t bus_generation;
    uint8_t slave_address;
    const uint8_t *tx;
    uint16_t tx_len;
    uint8_t *rx;
    uint16_t rx_len;
    uint64_t deadline_us;
    uint8_t priority;
    uint64_t admission_sequence;
} I2cPendingTransaction;

typedef struct {
    I2cPort port; /* Borrowed implementation copied by value. */
    bool port_bound;

    I2cBusClient clients[I2C_BUS_MAX_CLIENTS];
    uint8_t client_count;

    bool busy;
    I2cPendingTransaction active;
    I2cPendingTransaction pending[I2C_BUS_PENDING_CAPACITY];
    uint8_t pending_count;
    uint64_t next_admission_sequence;
    uint32_t next_transaction_id;

    uint32_t bus_generation;
    uint32_t arbitration_count;
    uint32_t timeout_count;
    uint32_t recovery_count;
    uint32_t stale_completion_count;
    uint32_t rejected_count;
    uint32_t completed_count;
} I2cBusManager;

void i2c_bus_init(I2cBusManager *bus, const I2cPort *port);
bool i2c_bus_bind_port(I2cBusManager *bus, const I2cPort *port);
bool i2c_bus_register_client(I2cBusManager *bus,
                             const I2cBusClient *client);
bool i2c_bus_set_client_generation(I2cBusManager *bus,
                                   uint32_t client_id,
                                   uint32_t client_generation);

/* tx and rx remain caller-owned and must stay valid until terminal
 * completion. The manager assigns the physical transaction identity. */
I2cSubmitResult i2c_bus_submit(I2cBusManager *bus,
                               const I2cBusRequest *request,
                               uint32_t *transaction_id_out);

/* Physical adapters route their deferred terminal callback here. */
bool i2c_bus_on_port_completion(I2cBusManager *bus,
                                const I2cPortRequest *request,
                                PortStatus result);

/* Lower-level completion injection used by deterministic host tests. */
bool i2c_bus_complete(I2cBusManager *bus,
                      uint32_t transaction_id,
                      uint32_t correlation_id,
                      uint32_t client_generation,
                      uint32_t bus_generation,
                      I2cTransactionResult result);

/* Applies the active deadline. Call from cooperative runtime context. */
bool i2c_bus_tick(I2cBusManager *bus, uint64_t now_us);

uint8_t i2c_bus_cancel_client(I2cBusManager *bus, uint32_t client_id);
void i2c_bus_recover(I2cBusManager *bus);

uint8_t i2c_bus_pending_count(const I2cBusManager *bus);
const I2cPendingTransaction *i2c_bus_active(const I2cBusManager *bus);

#endif
