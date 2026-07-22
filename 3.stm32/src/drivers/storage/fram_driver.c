#include "drivers/storage/fram_driver.h"

#include <string.h>

/**
 * @brief Checks whether a storage operation token is valid.
 *
 * A valid token identifies both the request and the owner generation that is
 * allowed to receive its asynchronous completion.
 *
 * @param token Operation identity token to validate.
 *
 * @return true if every required token field is non-zero; otherwise false.
 */
static bool token_is_valid(StorageOperationToken token) {
    return token.operation_id != 0u && token.correlation_id != 0u
           && token.owner_generation != 0u;
}

/**
 * @brief Validates the F-RAM driver configuration.
 *
 * The address check accepts the supported FM24CL04B base addresses while
 * reserving address bit zero for the device block selector.
 *
 * @param config Configuration to validate.
 *
 * @return true if the configuration is supported; otherwise false.
 */
static bool config_is_valid(const FramConfig* config) {
    if (!config || config->client_id == 0u
        || config->capacity_bytes != FM24CL04B_SIZE_BYTES || config->max_chunk_bytes == 0u
        || config->max_chunk_bytes > FM24CL04B_MAX_CHUNK_BYTES)
        return false;

    /* A2/A1 may select 0x50, 0x52, 0x54 or 0x56. Bit zero is the
     * FM24CL04B block/page selector and must be clear in the base. */
    return (config->slave_address_base_7bit & 0x78u) == 0x50u
           && (config->slave_address_base_7bit & 0x01u) == 0u;
}

/**
 * @brief Checks whether a logical F-RAM address range is valid.
 *
 * An empty range may start one byte beyond the final address because it does
 * not access the device.
 *
 * @param address Initial logical F-RAM address.
 * @param length Number of bytes in the range.
 *
 * @return true if the complete range is within the device; otherwise false.
 */
static bool range_is_valid(uint16_t address, uint16_t length) {
    /* A zero-length request may point one byte past the last valid byte because
     * it does not access memory. Non-empty requests must remain fully inside
     * the 512-byte logical address space. */
    if (length == 0u)
        return address <= FM24CL04B_SIZE_BYTES;
    return address < FM24CL04B_SIZE_BYTES
           && length <= (uint16_t)(FM24CL04B_SIZE_BYTES - address);
}

/**
 * @brief Resolves the I2C slave address for a logical F-RAM address.
 *
 * FM24CL04B logical address bit A8 is encoded in bit zero of the 7-bit I2C
 * slave address.
 *
 * @param driver Driver instance.
 * @param logical_address Logical F-RAM address to resolve.
 *
 * @return Seven-bit I2C slave address for the selected 256-byte block.
 */
static uint8_t slave_address(const FramDriver* driver, uint16_t logical_address) {
    /* FM24CL04B exposes address bit A8 through bit zero of its 7-bit I2C slave
     * address. The transaction payload therefore carries only A7..A0. */
    return (uint8_t)(driver->config.slave_address_base_7bit
                     | ((logical_address >> 8u) & 0x01u));
}

/**
 * @brief Calculates the length of the next I2C transfer chunk.
 *
 * The result is limited by the remaining operation length, the configured
 * maximum chunk size, and the current 256-byte block boundary.
 *
 * @param driver Driver instance containing the active operation state.
 *
 * @return Number of bytes to transfer in the next I2C transaction.
 */
static uint16_t chunk_length(const FramDriver* driver) {
    uint16_t remaining =
        (uint16_t)(driver->requested_length - driver->transferred_length);
    uint16_t page_remaining =
        (uint16_t)(FM24CL04B_PAGE_BYTES
                   - (driver->current_address & (FM24CL04B_PAGE_BYTES - 1u)));
    /* Limit each transfer both to the configured bus chunk size and to the
     * current 256-byte block. Crossing the boundary requires a new slave
     * address because A8 is encoded there. */
    uint16_t chunk = remaining;
    if (chunk > driver->config.max_chunk_bytes)
        chunk = driver->config.max_chunk_bytes;
    if (chunk > page_remaining)
        chunk = page_remaining;
    return chunk;
}

/**
 * @brief Maps an I2C transaction result to a storage I/O result.
 *
 * @param result I2C transaction result to convert.
 *
 * @return Equivalent storage I/O completion result.
 */
static StorageIoResult map_result(I2cTransactionResult result) {
    if (result == I2C_TRANSACTION_OK)
        return STORAGE_IO_RESULT_OK;
    if (result == I2C_TRANSACTION_TIMEOUT)
        return STORAGE_IO_RESULT_TIMEOUT;
    if (result == I2C_TRANSACTION_CANCELLED)
        return STORAGE_IO_RESULT_CANCELLED;
    if (result == I2C_TRANSACTION_STALE)
        return STORAGE_IO_RESULT_STALE;
    return STORAGE_IO_RESULT_BUS_ERROR;
}

/**
 * @brief Clears the active operation and returns the driver to idle state.
 *
 * Configuration, callback binding, generation values, and diagnostic counters
 * are preserved. The caller must copy any required completion data first.
 *
 * @param driver Driver instance to reset.
 */
static void reset_operation(FramDriver* driver) {
    /* Completion metadata is copied before this function is called, so all
     * per-request state can be cleared before invoking client code. */
    driver->state = FRAM_STATE_IDLE;
    driver->operation = FRAM_OPERATION_NONE;
    memset(&driver->token, 0, sizeof(driver->token));
    driver->start_address = 0u;
    driver->current_address = 0u;
    driver->requested_length = 0u;
    driver->transferred_length = 0u;
    driver->active_chunk_length = 0u;
    driver->deadline_us = 0u;
    driver->active_transaction_id = 0u;
    driver->read_buffer = NULL;
    driver->write_buffer = NULL;
    driver->probe_page = 0u;
}

/**
 * @brief Finalizes the active operation and notifies the bound client.
 *
 * The function snapshots completion metadata, updates diagnostic counters,
 * resets the driver, and then invokes the callback. Resetting first allows the
 * callback to submit a new operation immediately.
 *
 * @param driver Driver instance owning the active operation.
 * @param result Terminal result to report.
 */
static void emit_completion(FramDriver* driver, StorageIoResult result) {
    /* Build a stable snapshot because reset_operation() clears the live driver
     * state before the user callback runs. */
    StorageIoCompletion completion = {.token = driver->token,
                                      .result = result,
                                      .requested_length = driver->requested_length,
                                      .transferred_length = driver->transferred_length,
                                      .last_transaction_id =
                                          driver->active_transaction_id,
                                      .client_generation = driver->client_generation,
                                      .bus_generation = driver->last_bus_generation};
    if (result == STORAGE_IO_RESULT_OK)
        driver->completed_count++;
    else if (result == STORAGE_IO_RESULT_TIMEOUT)
        driver->timeout_count++;
    else if (result == STORAGE_IO_RESULT_CANCELLED)
        driver->cancelled_count++;
    else if (result == STORAGE_IO_RESULT_STALE)
        driver->stale_count++;
    else
        driver->bus_error_count++;

    /* Reset first so the completion callback may immediately submit another
     * operation without observing the driver as busy. */
    StorageIoCompletionFn completion_fn = driver->completion_fn;
    void* completion_context = driver->completion_context;
    reset_operation(driver);
    if (completion_fn)
        completion_fn(completion_context, &completion);
}

/**
 * @brief Maps an I2C submission result to a storage submission result.
 *
 * @param result I2C bus submission result to convert.
 *
 * @return Equivalent storage I/O submission result.
 */
static StorageIoSubmitResult map_submit_result(I2cSubmitResult result) {
    if (result == I2C_SUBMIT_ACCEPTED)
        return STORAGE_IO_SUBMIT_ACCEPTED;
    if (result == I2C_SUBMIT_NO_CAPACITY)
        return STORAGE_IO_SUBMIT_NO_CAPACITY;
    if (result == I2C_SUBMIT_INVALID_PARAM || result == I2C_SUBMIT_ADDRESS_REJECTED)
        return STORAGE_IO_SUBMIT_INVALID_PARAM;
    if (result == I2C_SUBMIT_NOT_READY || result == I2C_SUBMIT_UNKNOWN_CLIENT)
        return STORAGE_IO_SUBMIT_NOT_READY;
    return STORAGE_IO_SUBMIT_BUSY;
}

/**
 * @brief Builds and submits the next I2C transaction for the active operation.
 *
 * For reads and writes, the function selects the next data chunk without
 * crossing an FM24CL04B block boundary. For probe operations, it reads one
 * byte from the selected block. Buffers referenced by the request remain owned
 * by the driver or by the caller of the active asynchronous operation.
 *
 * @param driver Driver instance containing the active operation state.
 *
 * @return Submission result for the generated I2C transaction.
 */
static StorageIoSubmitResult submit_current_chunk(FramDriver* driver) {
    /* tx_buffer[0] always contains the 8-bit word address. A read transmits
     * that byte before receiving data; a write appends its payload after it. */
    const uint8_t* tx = driver->tx_buffer;
    uint16_t tx_length = 1u;
    uint8_t* rx = NULL;
    uint16_t rx_length = 0u;
    uint16_t address = driver->current_address;

    if (driver->operation == FRAM_OPERATION_PROBE) {
        /* Probe both 256-byte blocks because each one uses a different I2C
         * slave address. Reading one byte is non-destructive. */
        address = driver->probe_page == 0u ? 0u : FM24CL04B_PAGE_BYTES;
        driver->active_chunk_length = 1u;
        rx = &driver->probe_byte;
        rx_length = 1u;
    } else {
        driver->active_chunk_length = chunk_length(driver);
        if (driver->operation == FRAM_OPERATION_READ) {
            rx = driver->read_buffer + driver->transferred_length;
            rx_length = driver->active_chunk_length;
        } else {
            memcpy(driver->tx_buffer + 1u,
                   driver->write_buffer + driver->transferred_length,
                   driver->active_chunk_length);
            tx_length = (uint16_t)(1u + driver->active_chunk_length);
        }
    }

    /* A8 was moved into the slave address by slave_address(). */
    driver->tx_buffer[0] = (uint8_t)(address & 0xFFu);
    I2cBusRequest request = {.client_id = driver->config.client_id,
                             .correlation_id = driver->token.correlation_id,
                             .client_generation = driver->client_generation,
                             .slave_address = slave_address(driver, address),
                             .tx = tx,
                             .tx_length = tx_length,
                             .rx = rx,
                             .rx_length = rx_length,
                             .deadline_us = driver->deadline_us,
                             .priority = driver->config.bus_priority};
    uint32_t transaction_id = 0u;
    I2cSubmitResult result = i2c_bus_submit(driver->bus, &request, &transaction_id);
    if (result != I2C_SUBMIT_ACCEPTED)
        return map_submit_result(result);

    driver->active_transaction_id = transaction_id;
    /* Capture the generation assigned by the bus manager so diagnostics can
     * distinguish this transaction from work issued before bus recovery. */
    const I2cPendingTransaction* active = i2c_bus_active(driver->bus);
    driver->last_bus_generation = active && active->transaction_id == transaction_id
                                      ? active->bus_generation
                                      : driver->bus->bus_generation;
    driver->state = FRAM_STATE_WAITING_I2C;
    return STORAGE_IO_SUBMIT_ACCEPTED;
}

/**
 * @brief Validates, initializes, and starts an asynchronous F-RAM operation.
 *
 * The read or write buffer remains caller-owned and must stay valid until
 * terminal completion. A zero-length request completes without accessing the
 * I2C bus while preserving the asynchronous completion contract.
 *
 * @param driver Driver instance.
 * @param operation Operation type to start.
 * @param address Initial logical F-RAM address.
 * @param read_buffer Destination buffer for a read, or NULL otherwise.
 * @param write_buffer Source buffer for a write, or NULL otherwise.
 * @param length Number of bytes to transfer.
 * @param token Operation identity token.
 * @param deadline_us Absolute operation deadline.
 *
 * @return Submission result.
 */
static StorageIoSubmitResult begin_operation(FramDriver* driver,
                                             FramOperation operation,
                                             uint16_t address,
                                             uint8_t* read_buffer,
                                             const uint8_t* write_buffer,
                                             uint16_t length,
                                             StorageOperationToken token,
                                             uint64_t deadline_us) {
    /* The driver owns one in-flight logical operation. A logical operation may
     * consist of several I2C transactions submitted one after another. */
    if (!driver || driver->state == FRAM_STATE_UNINITIALIZED
        || driver->state == FRAM_STATE_FAULT || !driver->bus)
        return STORAGE_IO_SUBMIT_NOT_READY;
    if (driver->state != FRAM_STATE_IDLE)
        return STORAGE_IO_SUBMIT_BUSY;
    if (!token_is_valid(token) || deadline_us == 0u
        || (length > 0u && operation == FRAM_OPERATION_READ && !read_buffer)
        || (length > 0u && operation == FRAM_OPERATION_WRITE && !write_buffer))
        return STORAGE_IO_SUBMIT_INVALID_PARAM;
    if (!range_is_valid(address, length)) {
        driver->range_error_count++;
        return STORAGE_IO_SUBMIT_OUT_OF_RANGE;
    }

    driver->operation = operation;
    driver->token = token;
    driver->start_address = address;
    driver->current_address = address;
    driver->requested_length = length;
    driver->transferred_length = 0u;
    driver->deadline_us = deadline_us;
    driver->read_buffer = read_buffer;
    driver->write_buffer = write_buffer;
    driver->submitted_count++;

    if (length == 0u) {
        /* Preserve asynchronous API semantics while avoiding an unnecessary
         * bus request for an empty read or write. */
        emit_completion(driver, STORAGE_IO_RESULT_OK);
        return STORAGE_IO_SUBMIT_ACCEPTED;
    }

    StorageIoSubmitResult result = submit_current_chunk(driver);
    if (result != STORAGE_IO_SUBMIT_ACCEPTED)
        reset_operation(driver);
    return result;
}

/**
 * @brief Forwards an I2C bus completion to the F-RAM state machine.
 *
 * This callback is registered with I2cBusManager during driver initialization.
 * Both pointers are borrowed and are valid only for the duration of the call.
 *
 * @param context Callback context containing the F-RAM driver instance.
 * @param completion Completed I2C transaction metadata.
 */
static void bus_completion(void* context, const I2cTransactionCompletion* completion) {
    /* Adapter registered with I2cBusManager; all validation and state-machine
     * advancement remain inside the public FRAM completion handler. */
    fram_on_i2c_completion((FramDriver*)context, completion);
}

StorageIoSubmitResult
fram_init(FramDriver* driver, I2cBusManager* bus, const FramConfig* config) {
    if (!driver || !bus || !config_is_valid(config))
        return STORAGE_IO_SUBMIT_INVALID_PARAM;
    memset(driver, 0, sizeof(*driver));
    driver->bus = bus;
    driver->config = *config;
    driver->client_generation = 1u;

    /* address_mask 0x7E allows the bus manager to accept both block addresses:
     * base | 0 for bytes 0..255 and base | 1 for bytes 256..511. */
    I2cBusClient client = {.client_id = config->client_id,
                           .client_generation = driver->client_generation,
                           .address_base = config->slave_address_base_7bit,
                           .address_mask = 0x7Eu,
                           .context = driver,
                           .on_complete = bus_completion};
    if (!i2c_bus_register_client(bus, &client)) {
        driver->state = FRAM_STATE_UNINITIALIZED;
        return STORAGE_IO_SUBMIT_NOT_READY;
    }
    driver->state = FRAM_STATE_IDLE;
    return STORAGE_IO_SUBMIT_ACCEPTED;
}

StorageIoSubmitResult
fram_probe_async(FramDriver* driver, StorageOperationToken token, uint64_t deadline_us) {
    /* requested_length is two because a successful probe completes only after
     * one byte has been read from each addressable block. */
    StorageIoSubmitResult result = begin_operation(
        driver, FRAM_OPERATION_PROBE, 0u, NULL, NULL, 2u, token, deadline_us);
    if (result == STORAGE_IO_SUBMIT_ACCEPTED)
        driver->probe_page = 0u;
    return result;
}

StorageIoSubmitResult fram_read_async(FramDriver* driver,
                                      uint16_t address,
                                      uint8_t* buffer,
                                      uint16_t length,
                                      StorageOperationToken token,
                                      uint64_t deadline_us) {
    return begin_operation(
        driver, FRAM_OPERATION_READ, address, buffer, NULL, length, token, deadline_us);
}

StorageIoSubmitResult fram_write_async(FramDriver* driver,
                                       uint16_t address,
                                       const uint8_t* buffer,
                                       uint16_t length,
                                       StorageOperationToken token,
                                       uint64_t deadline_us) {
    return begin_operation(
        driver, FRAM_OPERATION_WRITE, address, NULL, buffer, length, token, deadline_us);
}

void fram_on_i2c_completion(FramDriver* driver,
                            const I2cTransactionCompletion* completion) {
    /* Ignore late, duplicated or foreign completions. Generation checking is
     * essential after cancellation because transaction IDs may outlive the
     * logical owner that originally submitted them. */
    if (!driver || !completion || driver->state != FRAM_STATE_WAITING_I2C
        || completion->client_id != driver->config.client_id
        || completion->transaction_id != driver->active_transaction_id
        || completion->correlation_id != driver->token.correlation_id
        || completion->client_generation != driver->client_generation) {
        if (driver)
            driver->stale_count++;
        return;
    }

    driver->last_bus_generation = completion->bus_generation;
    StorageIoResult result = map_result(completion->result);
    if (result != STORAGE_IO_RESULT_OK) {
        emit_completion(driver, result);
        return;
    }

    if (driver->operation == FRAM_OPERATION_PROBE) {
        driver->transferred_length++;
        if (driver->probe_page == 0u) {
            /* The first block responded; verify the second block before
             * reporting that the whole 512-byte device is available. */
            driver->probe_page = 1u;
            StorageIoSubmitResult submit = submit_current_chunk(driver);
            if (submit != STORAGE_IO_SUBMIT_ACCEPTED)
                emit_completion(driver, STORAGE_IO_RESULT_BUS_ERROR);
            return;
        }
        emit_completion(driver, STORAGE_IO_RESULT_OK);
        return;
    }

    /* Advance only after the active I2C transaction completed successfully.
     * submit_current_chunk() then derives the next block address and buffers. */
    driver->transferred_length =
        (uint16_t)(driver->transferred_length + driver->active_chunk_length);
    driver->current_address =
        (uint16_t)(driver->start_address + driver->transferred_length);
    if (driver->transferred_length < driver->requested_length) {
        StorageIoSubmitResult submit = submit_current_chunk(driver);
        if (submit != STORAGE_IO_SUBMIT_ACCEPTED)
            emit_completion(driver, STORAGE_IO_RESULT_BUS_ERROR);
        return;
    }
    emit_completion(driver, STORAGE_IO_RESULT_OK);
}

void fram_cancel_generation(FramDriver* driver, uint32_t new_generation) {
    if (!driver || new_generation == 0u || driver->state == FRAM_STATE_UNINITIALIZED)
        return;
    /* Bus cancellation eventually produces a completion for the old
     * generation. Updating the generation makes any later old completion
     * stale and prevents it from being accepted as current work. */
    if (fram_is_busy(driver))
        (void)i2c_bus_cancel_client(driver->bus, driver->config.client_id);
    driver->client_generation = new_generation;
    (void)i2c_bus_set_client_generation(
        driver->bus, driver->config.client_id, new_generation);
}

bool fram_is_busy(const FramDriver* driver) {
    return driver && driver->state == FRAM_STATE_WAITING_I2C;
}

/**
 * @brief Binds the completion callback used by the storage-port adapter.
 *
 * Binding is rejected while an operation is active so an in-flight completion
 * cannot be delivered to a different owner. The callback and context remain
 * caller-owned and must stay valid while bound.
 *
 * @param context Adapter context containing the F-RAM driver instance.
 * @param completion_fn Completion callback to bind.
 * @param completion_context Context passed to the completion callback.
 *
 * @return true if the callback was bound; otherwise false.
 */
static bool bind_completion(void* context,
                            StorageIoCompletionFn completion_fn,
                            void* completion_context) {
    /* Rebinding while busy could route an in-flight completion to the wrong
     * owner, so it is allowed only while the driver is idle. */
    FramDriver* driver = context;
    if (!driver || !completion_fn || fram_is_busy(driver))
        return false;
    driver->completion_fn = completion_fn;
    driver->completion_context = completion_context;
    return true;
}

/**
 * @brief Adapts a storage-port read request to the F-RAM driver API.
 *
 * The destination buffer remains caller-owned and must stay valid until
 * terminal completion.
 *
 * @param context Adapter context containing the F-RAM driver instance.
 * @param offset Initial storage offset.
 * @param buffer Destination buffer.
 * @param size Number of bytes to read.
 * @param token Operation identity token.
 * @param deadline_us Absolute operation deadline.
 *
 * @return Submission result.
 */
static StorageIoSubmitResult port_read(void* context,
                                       uint32_t offset,
                                       uint8_t* buffer,
                                       uint16_t size,
                                       StorageOperationToken token,
                                       uint64_t deadline_us) {
    /* StoragePort uses a 32-bit offset, whereas this device driver exposes the
     * FM24CL04B logical address as uint16_t. */
    if (offset > UINT16_MAX)
        return STORAGE_IO_SUBMIT_OUT_OF_RANGE;
    return fram_read_async(context, (uint16_t)offset, buffer, size, token, deadline_us);
}

/**
 * @brief Adapts a storage-port write request to the F-RAM driver API.
 *
 * The source buffer remains caller-owned and must stay valid until terminal
 * completion.
 *
 * @param context Adapter context containing the F-RAM driver instance.
 * @param offset Initial storage offset.
 * @param data Source buffer.
 * @param size Number of bytes to write.
 * @param token Operation identity token.
 * @param deadline_us Absolute operation deadline.
 *
 * @return Submission result.
 */
static StorageIoSubmitResult port_write(void* context,
                                        uint32_t offset,
                                        const uint8_t* data,
                                        uint16_t size,
                                        StorageOperationToken token,
                                        uint64_t deadline_us) {
    if (offset > UINT16_MAX)
        return STORAGE_IO_SUBMIT_OUT_OF_RANGE;
    return fram_write_async(context, (uint16_t)offset, data, size, token, deadline_us);
}

/**
 * @brief Adapts a storage-port generation cancellation request.
 *
 * @param context Adapter context containing the F-RAM driver instance.
 * @param new_generation New non-zero client generation.
 */
static void port_cancel_generation(void* context, uint32_t new_generation) {
    fram_cancel_generation(context, new_generation);
}

/**
 * @brief Reports whether the F-RAM driver has an active I2C transaction.
 *
 * @param context Adapter context containing the F-RAM driver instance.
 *
 * @return true if the driver is busy; otherwise false.
 */
static bool port_is_busy(const void* context) {
    return fram_is_busy(context);
}

bool fram_make_storage_port(FramDriver* driver, StoragePort* port_out) {
    if (!driver || !port_out || driver->state == FRAM_STATE_UNINITIALIZED)
        return false;
    /* Expose the concrete FRAM driver through the storage abstraction without
     * transferring ownership of the driver object. */
    *port_out = (StoragePort){.context = driver,
                              .bind_completion = bind_completion,
                              .read_async = port_read,
                              .write_async = port_write,
                              .cancel_generation = port_cancel_generation,
                              .is_busy = port_is_busy};
    return true;
}
