#include "drivers/storage/fram_driver.h"

#include <string.h>

static bool token_is_valid(StorageOperationToken token)
{
    return token.operation_id != 0u && token.correlation_id != 0u &&
           token.owner_generation != 0u;
}

static bool config_is_valid(const FramConfig *config)
{
    if (!config || config->client_id == 0u ||
        config->capacity_bytes != FM24CL04B_SIZE_BYTES ||
        config->max_chunk_bytes == 0u ||
        config->max_chunk_bytes > FM24CL04B_MAX_CHUNK_BYTES)
        return false;

    /* A2/A1 may select 0x50, 0x52, 0x54 or 0x56. Bit zero is the
     * FM24CL04B block/page selector and must be clear in the base. */
    return (config->slave_address_base_7bit & 0x78u) == 0x50u &&
           (config->slave_address_base_7bit & 0x01u) == 0u;
}

static bool range_is_valid(uint16_t address, uint16_t length)
{
    if (length == 0u)
        return address <= FM24CL04B_SIZE_BYTES;
    return address < FM24CL04B_SIZE_BYTES &&
           length <= (uint16_t)(FM24CL04B_SIZE_BYTES - address);
}

static uint8_t slave_address(const FramDriver *driver,
                             uint16_t logical_address)
{
    return (uint8_t)(driver->config.slave_address_base_7bit |
                     ((logical_address >> 8u) & 0x01u));
}

static uint16_t chunk_length(const FramDriver *driver)
{
    uint16_t remaining = (uint16_t)(driver->requested_length -
                                    driver->transferred_length);
    uint16_t page_remaining = (uint16_t)(FM24CL04B_PAGE_BYTES -
        (driver->current_address & (FM24CL04B_PAGE_BYTES - 1u)));
    uint16_t chunk = remaining;
    if (chunk > driver->config.max_chunk_bytes)
        chunk = driver->config.max_chunk_bytes;
    if (chunk > page_remaining)
        chunk = page_remaining;
    return chunk;
}

static StorageIoResult map_result(I2cTransactionResult result)
{
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

static void reset_operation(FramDriver *driver)
{
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

static void emit_completion(FramDriver *driver, StorageIoResult result)
{
    StorageIoCompletion completion = {
        .token = driver->token,
        .result = result,
        .requested_length = driver->requested_length,
        .transferred_length = driver->transferred_length,
        .last_transaction_id = driver->active_transaction_id,
        .client_generation = driver->client_generation,
        .bus_generation = driver->last_bus_generation
    };
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

    StorageIoCompletionFn completion_fn = driver->completion_fn;
    void *completion_context = driver->completion_context;
    reset_operation(driver);
    if (completion_fn)
        completion_fn(completion_context, &completion);
}

static StorageIoSubmitResult map_submit_result(I2cSubmitResult result)
{
    if (result == I2C_SUBMIT_ACCEPTED)
        return STORAGE_IO_SUBMIT_ACCEPTED;
    if (result == I2C_SUBMIT_NO_CAPACITY)
        return STORAGE_IO_SUBMIT_NO_CAPACITY;
    if (result == I2C_SUBMIT_INVALID_PARAM ||
        result == I2C_SUBMIT_ADDRESS_REJECTED)
        return STORAGE_IO_SUBMIT_INVALID_PARAM;
    if (result == I2C_SUBMIT_NOT_READY ||
        result == I2C_SUBMIT_UNKNOWN_CLIENT)
        return STORAGE_IO_SUBMIT_NOT_READY;
    return STORAGE_IO_SUBMIT_BUSY;
}

static StorageIoSubmitResult submit_current_chunk(FramDriver *driver)
{
    const uint8_t *tx = driver->tx_buffer;
    uint16_t tx_length = 1u;
    uint8_t *rx = NULL;
    uint16_t rx_length = 0u;
    uint16_t address = driver->current_address;

    if (driver->operation == FRAM_OPERATION_PROBE) {
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

    driver->tx_buffer[0] = (uint8_t)(address & 0xFFu);
    I2cBusRequest request = {
        .client_id = driver->config.client_id,
        .correlation_id = driver->token.correlation_id,
        .client_generation = driver->client_generation,
        .slave_address = slave_address(driver, address),
        .tx = tx,
        .tx_length = tx_length,
        .rx = rx,
        .rx_length = rx_length,
        .deadline_us = driver->deadline_us,
        .priority = driver->config.bus_priority
    };
    uint32_t transaction_id = 0u;
    I2cSubmitResult result = i2c_bus_submit(driver->bus, &request,
                                            &transaction_id);
    if (result != I2C_SUBMIT_ACCEPTED)
        return map_submit_result(result);

    driver->active_transaction_id = transaction_id;
    const I2cPendingTransaction *active = i2c_bus_active(driver->bus);
    driver->last_bus_generation = active &&
        active->transaction_id == transaction_id
        ? active->bus_generation : driver->bus->bus_generation;
    driver->state = FRAM_STATE_WAITING_I2C;
    return STORAGE_IO_SUBMIT_ACCEPTED;
}

static StorageIoSubmitResult begin_operation(
    FramDriver *driver,
    FramOperation operation,
    uint16_t address,
    uint8_t *read_buffer,
    const uint8_t *write_buffer,
    uint16_t length,
    StorageOperationToken token,
    uint64_t deadline_us)
{
    if (!driver || driver->state == FRAM_STATE_UNINITIALIZED ||
        driver->state == FRAM_STATE_FAULT || !driver->bus)
        return STORAGE_IO_SUBMIT_NOT_READY;
    if (driver->state != FRAM_STATE_IDLE)
        return STORAGE_IO_SUBMIT_BUSY;
    if (!token_is_valid(token) || deadline_us == 0u ||
        (length > 0u && operation == FRAM_OPERATION_READ && !read_buffer) ||
        (length > 0u && operation == FRAM_OPERATION_WRITE && !write_buffer))
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
        emit_completion(driver, STORAGE_IO_RESULT_OK);
        return STORAGE_IO_SUBMIT_ACCEPTED;
    }

    StorageIoSubmitResult result = submit_current_chunk(driver);
    if (result != STORAGE_IO_SUBMIT_ACCEPTED)
        reset_operation(driver);
    return result;
}

static void bus_completion(void *context,
                           const I2cTransactionCompletion *completion)
{
    fram_on_i2c_completion((FramDriver *)context, completion);
}

StorageIoSubmitResult fram_init(FramDriver *driver,
                                I2cBusManager *bus,
                                const FramConfig *config)
{
    if (!driver || !bus || !config_is_valid(config))
        return STORAGE_IO_SUBMIT_INVALID_PARAM;
    memset(driver, 0, sizeof(*driver));
    driver->bus = bus;
    driver->config = *config;
    driver->client_generation = 1u;

    I2cBusClient client = {
        .client_id = config->client_id,
        .client_generation = driver->client_generation,
        .address_base = config->slave_address_base_7bit,
        .address_mask = 0x7Eu,
        .context = driver,
        .on_complete = bus_completion
    };
    if (!i2c_bus_register_client(bus, &client)) {
        driver->state = FRAM_STATE_UNINITIALIZED;
        return STORAGE_IO_SUBMIT_NOT_READY;
    }
    driver->state = FRAM_STATE_IDLE;
    return STORAGE_IO_SUBMIT_ACCEPTED;
}

StorageIoSubmitResult fram_probe_async(FramDriver *driver,
                                       StorageOperationToken token,
                                       uint64_t deadline_us)
{
    StorageIoSubmitResult result = begin_operation(
        driver, FRAM_OPERATION_PROBE, 0u, NULL, NULL, 2u, token,
        deadline_us);
    if (result == STORAGE_IO_SUBMIT_ACCEPTED)
        driver->probe_page = 0u;
    return result;
}

StorageIoSubmitResult fram_read_async(FramDriver *driver,
                                      uint16_t address,
                                      uint8_t *buffer,
                                      uint16_t length,
                                      StorageOperationToken token,
                                      uint64_t deadline_us)
{
    return begin_operation(driver, FRAM_OPERATION_READ, address, buffer,
                           NULL, length, token, deadline_us);
}

StorageIoSubmitResult fram_write_async(FramDriver *driver,
                                       uint16_t address,
                                       const uint8_t *buffer,
                                       uint16_t length,
                                       StorageOperationToken token,
                                       uint64_t deadline_us)
{
    return begin_operation(driver, FRAM_OPERATION_WRITE, address, NULL,
                           buffer, length, token, deadline_us);
}

void fram_on_i2c_completion(
    FramDriver *driver,
    const I2cTransactionCompletion *completion)
{
    if (!driver || !completion || driver->state != FRAM_STATE_WAITING_I2C ||
        completion->client_id != driver->config.client_id ||
        completion->transaction_id != driver->active_transaction_id ||
        completion->correlation_id != driver->token.correlation_id ||
        completion->client_generation != driver->client_generation) {
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
            driver->probe_page = 1u;
            StorageIoSubmitResult submit = submit_current_chunk(driver);
            if (submit != STORAGE_IO_SUBMIT_ACCEPTED)
                emit_completion(driver, STORAGE_IO_RESULT_BUS_ERROR);
            return;
        }
        emit_completion(driver, STORAGE_IO_RESULT_OK);
        return;
    }

    driver->transferred_length = (uint16_t)(driver->transferred_length +
                                             driver->active_chunk_length);
    driver->current_address = (uint16_t)(driver->start_address +
                                         driver->transferred_length);
    if (driver->transferred_length < driver->requested_length) {
        StorageIoSubmitResult submit = submit_current_chunk(driver);
        if (submit != STORAGE_IO_SUBMIT_ACCEPTED)
            emit_completion(driver, STORAGE_IO_RESULT_BUS_ERROR);
        return;
    }
    emit_completion(driver, STORAGE_IO_RESULT_OK);
}

void fram_cancel_generation(FramDriver *driver, uint32_t new_generation)
{
    if (!driver || new_generation == 0u ||
        driver->state == FRAM_STATE_UNINITIALIZED)
        return;
    if (fram_is_busy(driver))
        (void)i2c_bus_cancel_client(driver->bus, driver->config.client_id);
    driver->client_generation = new_generation;
    (void)i2c_bus_set_client_generation(driver->bus,
                                         driver->config.client_id,
                                         new_generation);
}

bool fram_is_busy(const FramDriver *driver)
{
    return driver && driver->state == FRAM_STATE_WAITING_I2C;
}

static bool bind_completion(void *context,
                            StorageIoCompletionFn completion_fn,
                            void *completion_context)
{
    FramDriver *driver = context;
    if (!driver || !completion_fn || fram_is_busy(driver))
        return false;
    driver->completion_fn = completion_fn;
    driver->completion_context = completion_context;
    return true;
}

static StorageIoSubmitResult port_read(void *context,
                                       uint32_t offset,
                                       uint8_t *buffer,
                                       uint16_t size,
                                       StorageOperationToken token,
                                       uint64_t deadline_us)
{
    if (offset > UINT16_MAX)
        return STORAGE_IO_SUBMIT_OUT_OF_RANGE;
    return fram_read_async(context, (uint16_t)offset, buffer, size, token,
                           deadline_us);
}

static StorageIoSubmitResult port_write(void *context,
                                        uint32_t offset,
                                        const uint8_t *data,
                                        uint16_t size,
                                        StorageOperationToken token,
                                        uint64_t deadline_us)
{
    if (offset > UINT16_MAX)
        return STORAGE_IO_SUBMIT_OUT_OF_RANGE;
    return fram_write_async(context, (uint16_t)offset, data, size, token,
                            deadline_us);
}

static void port_cancel_generation(void *context, uint32_t new_generation)
{
    fram_cancel_generation(context, new_generation);
}

static bool port_is_busy(const void *context)
{
    return fram_is_busy(context);
}

bool fram_make_storage_port(FramDriver *driver, StoragePort *port_out)
{
    if (!driver || !port_out || driver->state == FRAM_STATE_UNINITIALIZED)
        return false;
    *port_out = (StoragePort){
        .context = driver,
        .bind_completion = bind_completion,
        .read_async = port_read,
        .write_async = port_write,
        .cancel_generation = port_cancel_generation,
        .is_busy = port_is_busy
    };
    return true;
}
