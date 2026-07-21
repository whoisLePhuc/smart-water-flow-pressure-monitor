#ifndef SWFPM_STORAGE_PORT_H
#define SWFPM_STORAGE_PORT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t operation_id;
    uint32_t correlation_id;
    uint32_t owner_generation;
} StorageOperationToken;

typedef enum {
    STORAGE_IO_SUBMIT_ACCEPTED = 0,
    STORAGE_IO_SUBMIT_BUSY,
    STORAGE_IO_SUBMIT_INVALID_PARAM,
    STORAGE_IO_SUBMIT_OUT_OF_RANGE,
    STORAGE_IO_SUBMIT_NOT_READY,
    STORAGE_IO_SUBMIT_NO_CAPACITY
} StorageIoSubmitResult;

typedef enum {
    STORAGE_IO_RESULT_OK = 0,
    STORAGE_IO_RESULT_TIMEOUT,
    STORAGE_IO_RESULT_BUS_ERROR,
    STORAGE_IO_RESULT_CANCELLED,
    STORAGE_IO_RESULT_STALE,
    STORAGE_IO_RESULT_SHORT_TRANSFER,
    STORAGE_IO_RESULT_INTERNAL_ERROR
} StorageIoResult;

typedef struct {
    StorageOperationToken token;
    StorageIoResult result;
    uint16_t requested_length;
    uint16_t transferred_length;
    uint32_t last_transaction_id;
    uint32_t client_generation;
    uint32_t bus_generation;
} StorageIoCompletion;

typedef void (*StorageIoCompletionFn)(
    void *context,
    const StorageIoCompletion *completion);

/* Instance-owned asynchronous storage contract. Buffers remain caller-owned
 * until the matching terminal completion. Implementations must emit exactly
 * one terminal completion for every accepted operation. */
typedef struct {
    void *context;

    bool (*bind_completion)(void *context,
                            StorageIoCompletionFn completion_fn,
                            void *completion_context);

    StorageIoSubmitResult (*read_async)(
        void *context,
        uint32_t offset,
        uint8_t *buffer,
        uint16_t size,
        StorageOperationToken token,
        uint64_t deadline_us);

    StorageIoSubmitResult (*write_async)(
        void *context,
        uint32_t offset,
        const uint8_t *data,
        uint16_t size,
        StorageOperationToken token,
        uint64_t deadline_us);

    void (*cancel_generation)(void *context, uint32_t new_generation);
    bool (*is_busy)(const void *context);
} StoragePort;

static inline bool storage_port_is_valid(const StoragePort *port)
{
    return port && port->context && port->bind_completion &&
           port->read_async && port->write_async &&
           port->cancel_generation && port->is_busy;
}

#endif
