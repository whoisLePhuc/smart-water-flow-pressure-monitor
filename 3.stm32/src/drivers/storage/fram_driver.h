#ifndef SWFPM_FRAM_DRIVER_H
#define SWFPM_FRAM_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#include "infrastructure/bus/i2c_bus_manager.h"
#include "ports/storage_port.h"

/** Total FM24CL04B capacity in bytes. */
#define FM24CL04B_SIZE_BYTES 512u

/** Addressable block size selected through the slave address. */
#define FM24CL04B_PAGE_BYTES 256u

/** Maximum payload handled by one I2C transaction. */
#define FM24CL04B_MAX_CHUNK_BYTES 32u

/**
 * @brief Runtime configuration for the F-RAM driver.
 */
typedef struct {
    uint32_t client_id;              /**< Registered I2C bus client identifier. */
    uint8_t slave_address_base_7bit; /**< Base 7-bit FM24CL04B address. */
    uint16_t capacity_bytes;         /**< Accessible storage capacity. */
    uint16_t max_chunk_bytes;        /**< Maximum bytes per I2C transaction. */
    uint8_t bus_priority;            /**< I2C priority; lower values win. */
} FramConfig;

/**
 * @brief Current F-RAM driver state.
 */
typedef enum {
    FRAM_STATE_UNINITIALIZED = 0, /**< Driver has not been initialized. */
    FRAM_STATE_IDLE,              /**< Driver is ready for a new operation. */
    FRAM_STATE_WAITING_I2C,       /**< Driver is waiting for I2C completion. */
    FRAM_STATE_FAULT              /**< Driver entered a terminal fault state. */
} FramState;

/**
 * @brief Active F-RAM operation type.
 */
typedef enum {
    FRAM_OPERATION_NONE = 0, /**< No operation is active. */
    FRAM_OPERATION_PROBE,    /**< Device probe is active. */
    FRAM_OPERATION_READ,     /**< Read operation is active. */
    FRAM_OPERATION_WRITE     /**< Write operation is active. */
} FramOperation;

/**
 * @brief Runtime state of the asynchronous F-RAM driver.
 */
typedef struct {
    I2cBusManager* bus; /**< Borrowed; owner must outlive the driver. */

    FramConfig config;          /**< Driver configuration. */
    FramState state;            /**< Current driver state. */
    FramOperation operation;    /**< Current operation type. */
    uint32_t client_generation; /**< Current I2C client generation. */

    StorageOperationToken token;    /**< Active storage operation token. */
    uint16_t start_address;         /**< Initial operation address. */
    uint16_t current_address;       /**< Address of the next chunk. */
    uint16_t requested_length;      /**< Total requested byte count. */
    uint16_t transferred_length;    /**< Number of bytes completed. */
    uint16_t active_chunk_length;   /**< Size of the active I2C chunk. */
    uint64_t deadline_us;           /**< Absolute operation deadline. */
    uint32_t active_transaction_id; /**< Active I2C transaction identifier. */
    uint32_t last_bus_generation;   /**< Bus generation of the active request. */

    uint8_t* read_buffer;        /**< Borrowed until terminal completion. */
    const uint8_t* write_buffer; /**< Borrowed until terminal completion. */

    /** Address byte followed by the current write chunk. */
    uint8_t tx_buffer[1u + FM24CL04B_MAX_CHUNK_BYTES];

    uint8_t probe_byte; /**< Temporary byte used during probing. */
    uint8_t probe_page; /**< Address block currently being probed. */

    StorageIoCompletionFn completion_fn; /**< Terminal completion callback. */
    void* completion_context;            /**< Borrowed callback context. */

    uint32_t submitted_count;   /**< Number of submitted operations. */
    uint32_t completed_count;   /**< Number of successful operations. */
    uint32_t timeout_count;     /**< Number of timed-out operations. */
    uint32_t bus_error_count;   /**< Number of I2C failures. */
    uint32_t stale_count;       /**< Number of stale completions. */
    uint32_t cancelled_count;   /**< Number of cancelled operations. */
    uint32_t range_error_count; /**< Number of rejected address ranges. */
} FramDriver;

/**
 * @brief Initializes the F-RAM driver and registers its I2C client.
 *
 * @param driver Driver instance.
 * @param bus Shared I2C bus manager.
 * @param config Driver configuration.
 *
 * @return Initialization result.
 */
StorageIoSubmitResult
fram_init(FramDriver* driver, I2cBusManager* bus, const FramConfig* config);

/**
 * @brief Creates a generic storage port backed by the F-RAM driver.
 *
 * @param driver Initialized driver instance.
 * @param port_out Receives the storage port implementation.
 *
 * @return true if the port was created successfully.
 */
bool fram_make_storage_port(FramDriver* driver, StoragePort* port_out);

/**
 * @brief Starts an asynchronous device probe.
 *
 * @param driver Driver instance.
 * @param token Operation identity token.
 * @param deadline_us Absolute operation deadline.
 *
 * @return Submission result.
 */
StorageIoSubmitResult
fram_probe_async(FramDriver* driver, StorageOperationToken token, uint64_t deadline_us);

/**
 * @brief Starts an asynchronous F-RAM read.
 *
 * The destination buffer remains caller-owned and must stay valid until
 * terminal completion.
 *
 * @param driver Driver instance.
 * @param address Initial F-RAM address.
 * @param buffer Destination buffer.
 * @param length Number of bytes to read.
 * @param token Operation identity token.
 * @param deadline_us Absolute operation deadline.
 *
 * @return Submission result.
 */
StorageIoSubmitResult fram_read_async(FramDriver* driver,
                                      uint16_t address,
                                      uint8_t* buffer,
                                      uint16_t length,
                                      StorageOperationToken token,
                                      uint64_t deadline_us);

/**
 * @brief Starts an asynchronous F-RAM write.
 *
 * The source buffer remains caller-owned and must stay valid until terminal
 * completion.
 *
 * @param driver Driver instance.
 * @param address Initial F-RAM address.
 * @param buffer Source buffer.
 * @param length Number of bytes to write.
 * @param token Operation identity token.
 * @param deadline_us Absolute operation deadline.
 *
 * @return Submission result.
 */
StorageIoSubmitResult fram_write_async(FramDriver* driver,
                                       uint16_t address,
                                       const uint8_t* buffer,
                                       uint16_t length,
                                       StorageOperationToken token,
                                       uint64_t deadline_us);

/**
 * @brief Handles completion of the active I2C transaction.
 *
 * @param driver Driver instance.
 * @param completion I2C completion information.
 */
void fram_on_i2c_completion(FramDriver* driver,
                            const I2cTransactionCompletion* completion);

/**
 * @brief Cancels the current generation and invalidates pending completions.
 *
 * @param driver Driver instance.
 * @param new_generation New nonzero client generation.
 */
void fram_cancel_generation(FramDriver* driver, uint32_t new_generation);

/**
 * @brief Checks whether an operation is active.
 *
 * @return true if the driver is waiting for I2C completion.
 */
bool fram_is_busy(const FramDriver* driver);

#endif /* SWFPM_FRAM_DRIVER_H */