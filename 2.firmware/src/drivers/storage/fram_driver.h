#ifndef SWFPM_FRAM_DRIVER_H
#define SWFPM_FRAM_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#include "infrastructure/bus/i2c_bus_manager.h"
#include "ports/storage_port.h"

#define FM24CL04B_SIZE_BYTES 512u
#define FM24CL04B_PAGE_BYTES 256u
#define FM24CL04B_MAX_CHUNK_BYTES 32u

typedef struct {
    uint32_t client_id;
    uint8_t slave_address_base_7bit;
    uint16_t capacity_bytes;
    uint16_t max_chunk_bytes;
    uint8_t bus_priority;
} FramConfig;

typedef enum {
    FRAM_STATE_UNINITIALIZED = 0,
    FRAM_STATE_IDLE,
    FRAM_STATE_WAITING_I2C,
    FRAM_STATE_FAULT
} FramState;

typedef enum {
    FRAM_OPERATION_NONE = 0,
    FRAM_OPERATION_PROBE,
    FRAM_OPERATION_READ,
    FRAM_OPERATION_WRITE
} FramOperation;

typedef struct {
    I2cBusManager *bus; /* Borrowed; owner must outlive the driver. */
    FramConfig config;
    FramState state;
    FramOperation operation;
    uint32_t client_generation;

    StorageOperationToken token;
    uint16_t start_address;
    uint16_t current_address;
    uint16_t requested_length;
    uint16_t transferred_length;
    uint16_t active_chunk_length;
    uint64_t deadline_us;
    uint32_t active_transaction_id;
    uint32_t last_bus_generation;

    uint8_t *read_buffer;          /* Borrowed until terminal completion. */
    const uint8_t *write_buffer;   /* Borrowed until terminal completion. */
    uint8_t tx_buffer[1u + FM24CL04B_MAX_CHUNK_BYTES];
    uint8_t probe_byte;
    uint8_t probe_page;

    StorageIoCompletionFn completion_fn;
    void *completion_context;

    uint32_t submitted_count;
    uint32_t completed_count;
    uint32_t timeout_count;
    uint32_t bus_error_count;
    uint32_t stale_count;
    uint32_t cancelled_count;
    uint32_t range_error_count;
} FramDriver;

StorageIoSubmitResult fram_init(FramDriver *driver,
                                I2cBusManager *bus,
                                const FramConfig *config);

bool fram_make_storage_port(FramDriver *driver, StoragePort *port_out);

StorageIoSubmitResult fram_probe_async(FramDriver *driver,
                                       StorageOperationToken token,
                                       uint64_t deadline_us);

StorageIoSubmitResult fram_read_async(FramDriver *driver,
                                      uint16_t address,
                                      uint8_t *buffer,
                                      uint16_t length,
                                      StorageOperationToken token,
                                      uint64_t deadline_us);

StorageIoSubmitResult fram_write_async(FramDriver *driver,
                                       uint16_t address,
                                       const uint8_t *buffer,
                                       uint16_t length,
                                       StorageOperationToken token,
                                       uint64_t deadline_us);

void fram_on_i2c_completion(
    FramDriver *driver,
    const I2cTransactionCompletion *completion);

void fram_cancel_generation(FramDriver *driver, uint32_t new_generation);
bool fram_is_busy(const FramDriver *driver);

#endif /* SWFPM_FRAM_DRIVER_H */
