#ifndef SWFPM_I2C_BUS_MANAGER_H
#define SWFPM_I2C_BUS_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "ports/i2c_port.h"

/**
 * @brief Manages the physical I2C bus shared by multiple clients.
 *
 * Lower numeric priority values have higher priority. An active transaction
 * is never preempted.
 */

#define I2C_BUS_MAX_CLIENTS 4u      /**< Maximum number of registered clients. */
#define I2C_BUS_PENDING_CAPACITY 8u /**< Maximum number of queued transactions. */

/**
 * @brief Terminal result of an I2C transaction.
 */
typedef enum {
    I2C_TRANSACTION_OK,        /**< Transaction completed successfully. */
    I2C_TRANSACTION_BUSY,      /**< Port was busy. */
    I2C_TRANSACTION_TIMEOUT,   /**< Transaction exceeded its deadline. */
    I2C_TRANSACTION_FAILED,    /**< Transaction failed. */
    I2C_TRANSACTION_CANCELLED, /**< Transaction was cancelled. */
    I2C_TRANSACTION_STALE      /**< Transaction generation is no longer valid. */
} I2cTransactionResult;

/**
 * @brief Result of submitting an I2C transaction.
 */
typedef enum {
    I2C_SUBMIT_ACCEPTED = 0,     /**< Request was accepted. */
    I2C_SUBMIT_INVALID_PARAM,    /**< Request contains invalid parameters. */
    I2C_SUBMIT_NOT_READY,        /**< Bus manager is not ready. */
    I2C_SUBMIT_UNKNOWN_CLIENT,   /**< Client is not registered. */
    I2C_SUBMIT_ADDRESS_REJECTED, /**< Slave address is not allowed for the client. */
    I2C_SUBMIT_NO_CAPACITY,      /**< Pending queue is full. */
    I2C_SUBMIT_PORT_ERROR        /**< Physical port rejected the request. */
} I2cSubmitResult;

/**
 * @brief Completion information delivered to an I2C client.
 */
typedef struct {
    uint32_t client_id;          /**< Client that owns the transaction. */
    uint32_t transaction_id;     /**< Physical transaction identifier. */
    uint32_t correlation_id;     /**< Application correlation identifier. */
    uint32_t client_generation;  /**< Client generation at submission time. */
    uint32_t bus_generation;     /**< Bus generation at submission time. */
    I2cTransactionResult result; /**< Terminal transaction result. */
} I2cTransactionCompletion;

/**
 * @brief Registered I2C bus client.
 */
typedef struct I2cBusClient {
    uint32_t client_id;         /**< Unique client identifier. */
    uint32_t client_generation; /**< Current client generation. */
    uint8_t address_base;       /**< Expected slave address bits. */
    uint8_t address_mask;       /**< Mask used to validate slave addresses. */

    void* context; /**< Borrowed callback context owned by the client. */

    /**
     * @brief Handles terminal transaction completion.
     *
     * @param ctx Client callback context.
     * @param completion Transaction completion information.
     */
    void (*on_complete)(void* ctx, const I2cTransactionCompletion* completion);
} I2cBusClient;

/**
 * @brief Transaction request submitted by an I2C client.
 */
typedef struct {
    uint32_t client_id;         /**< Submitting client identifier. */
    uint32_t correlation_id;    /**< Application correlation identifier. */
    uint32_t client_generation; /**< Expected client generation. */

    uint8_t slave_address; /**< 7-bit I2C slave address. */

    const uint8_t* tx;  /**< Transmit buffer, or NULL if unused. */
    uint16_t tx_length; /**< Number of bytes to transmit. */

    uint8_t* rx;        /**< Receive buffer, or NULL if unused. */
    uint16_t rx_length; /**< Number of bytes to receive. */

    uint64_t deadline_us; /**< Absolute deadline in microseconds. */
    uint8_t priority;     /**< Arbitration priority; lower values win. */
} I2cBusRequest;

/**
 * @brief Internal representation of an admitted transaction.
 */
typedef struct {
    uint32_t client_id;         /**< Owning client identifier. */
    uint32_t transaction_id;    /**< Assigned physical transaction identifier. */
    uint32_t correlation_id;    /**< Application correlation identifier. */
    uint32_t client_generation; /**< Client generation at admission time. */
    uint32_t bus_generation;    /**< Bus generation at admission time. */

    uint8_t slave_address; /**< 7-bit I2C slave address. */

    const uint8_t* tx; /**< Borrowed transmit buffer. */
    uint16_t tx_len;   /**< Number of bytes to transmit. */

    uint8_t* rx;     /**< Borrowed receive buffer. */
    uint16_t rx_len; /**< Number of bytes to receive. */

    uint64_t deadline_us;        /**< Absolute deadline in microseconds. */
    uint8_t priority;            /**< Arbitration priority; lower values win. */
    uint64_t admission_sequence; /**< FIFO order among equal-priority requests. */
} I2cPendingTransaction;

/**
 * @brief Runtime state of the shared I2C bus manager.
 */
typedef struct {
    I2cPort port;    /**< Borrowed port implementation copied by value. */
    bool port_bound; /**< Indicates whether a port is bound. */

    I2cBusClient clients[I2C_BUS_MAX_CLIENTS]; /**< Registered clients. */
    uint8_t client_count;                      /**< Number of registered clients. */

    bool busy;                    /**< Indicates an active transaction. */
    I2cPendingTransaction active; /**< Current active transaction. */
    I2cPendingTransaction pending[I2C_BUS_PENDING_CAPACITY]; /**< Pending queue. */
    uint8_t pending_count; /**< Number of pending transactions. */

    uint64_t next_admission_sequence; /**< Next FIFO admission sequence. */
    uint32_t next_transaction_id;     /**< Next transaction identifier. */

    uint32_t bus_generation;         /**< Current bus recovery generation. */
    uint32_t arbitration_count;      /**< Number of arbitration decisions. */
    uint32_t timeout_count;          /**< Number of timed-out transactions. */
    uint32_t recovery_count;         /**< Number of bus recoveries. */
    uint32_t stale_completion_count; /**< Number of ignored stale completions. */
    uint32_t rejected_count;         /**< Number of rejected submissions. */
    uint32_t completed_count;        /**< Number of completed transactions. */
} I2cBusManager;

/**
 * @brief Initializes an I2C bus manager.
 *
 * @param bus Bus manager instance.
 * @param port Physical I2C port, or NULL.
 */
void i2c_bus_init(I2cBusManager* bus, const I2cPort* port);

/**
 * @brief Binds a physical I2C port to the manager.
 *
 * @param bus Bus manager instance.
 * @param port Physical I2C port.
 *
 * @return true if the port was bound successfully.
 */
bool i2c_bus_bind_port(I2cBusManager* bus, const I2cPort* port);

/**
 * @brief Registers an I2C client.
 *
 * @param bus Bus manager instance.
 * @param client Client definition.
 *
 * @return true if the client was registered successfully.
 */
bool i2c_bus_register_client(I2cBusManager* bus, const I2cBusClient* client);

/**
 * @brief Updates the generation of a registered client.
 *
 * @param bus Bus manager instance.
 * @param client_id Client identifier.
 * @param client_generation New client generation.
 *
 * @return true if the client was found and updated.
 */
bool i2c_bus_set_client_generation(I2cBusManager* bus,
                                   uint32_t client_id,
                                   uint32_t client_generation);

/**
 * @brief Submits an I2C transaction.
 *
 * The transmit and receive buffers remain caller-owned and must stay valid
 * until terminal completion.
 *
 * @param bus Bus manager instance.
 * @param request Transaction request.
 * @param transaction_id_out Receives the assigned transaction identifier.
 *
 * @return Submission result.
 */
I2cSubmitResult i2c_bus_submit(I2cBusManager* bus,
                               const I2cBusRequest* request,
                               uint32_t* transaction_id_out);

/**
 * @brief Handles a deferred physical-port completion.
 *
 * @param bus Bus manager instance.
 * @param request Completed physical-port request.
 * @param result Physical-port result.
 *
 * @return true if the completion matched the active transaction.
 */
bool i2c_bus_on_port_completion(I2cBusManager* bus,
                                const I2cPortRequest* request,
                                PortStatus result);

/**
 * @brief Injects a terminal completion.
 *
 * Primarily used by deterministic host tests.
 *
 * @return true if the completion matched the active transaction.
 */
bool i2c_bus_complete(I2cBusManager* bus,
                      uint32_t transaction_id,
                      uint32_t correlation_id,
                      uint32_t client_generation,
                      uint32_t bus_generation,
                      I2cTransactionResult result);

/**
 * @brief Processes the active transaction deadline.
 *
 * @param bus Bus manager instance.
 * @param now_us Current monotonic time in microseconds.
 *
 * @return true if the manager state changed.
 */
bool i2c_bus_tick(I2cBusManager* bus, uint64_t now_us);

/**
 * @brief Cancels all transactions owned by a client.
 *
 * @return Number of cancelled transactions.
 */
uint8_t i2c_bus_cancel_client(I2cBusManager* bus, uint32_t client_id);

/**
 * @brief Recovers the bus and invalidates outstanding transactions.
 */
void i2c_bus_recover(I2cBusManager* bus);

/**
 * @brief Returns the number of pending transactions.
 */
uint8_t i2c_bus_pending_count(const I2cBusManager* bus);

/**
 * @brief Returns the active transaction.
 *
 * @return Active transaction, or NULL if the bus is idle.
 */
const I2cPendingTransaction* i2c_bus_active(const I2cBusManager* bus);

#endif /* SWFPM_I2C_BUS_MANAGER_H */