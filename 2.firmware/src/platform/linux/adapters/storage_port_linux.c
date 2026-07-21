#include "storage_port_linux.h"

#include <string.h>

static bool bind_completion(void *context,
                            StorageIoCompletionFn completion_fn,
                            void *completion_context)
{
    LinuxStorageAdapter *adapter = context;
    if (!adapter || !completion_fn || adapter->active)
        return false;
    adapter->completion_fn = completion_fn;
    adapter->completion_context = completion_context;
    return true;
}

static StorageIoSubmitResult complete_operation(
    LinuxStorageAdapter *adapter,
    uint32_t offset,
    uint8_t *read_buffer,
    const uint8_t *write_buffer,
    uint16_t size,
    StorageOperationToken token)
{
    if (!adapter || !adapter->completion_fn || token.operation_id == 0u ||
        token.correlation_id == 0u || token.owner_generation == 0u ||
        (size > 0u && !read_buffer && !write_buffer))
        return STORAGE_IO_SUBMIT_INVALID_PARAM;
    if (adapter->active)
        return STORAGE_IO_SUBMIT_BUSY;
    if (offset > LINUX_STORAGE_CAPACITY_BYTES ||
        size > LINUX_STORAGE_CAPACITY_BYTES - offset)
        return STORAGE_IO_SUBMIT_OUT_OF_RANGE;

    adapter->active = true;
    if (size > 0u) {
        if (read_buffer) {
            memcpy(read_buffer, adapter->memory + offset, size);
        } else if (write_buffer) {
            memcpy(adapter->memory + offset, write_buffer, size);
        } else {
            adapter->active = false;
            return STORAGE_IO_SUBMIT_INVALID_PARAM;
        }
    }

    StorageIoCompletion completion = {
        .token = token,
        .result = STORAGE_IO_RESULT_OK,
        .requested_length = size,
        .transferred_length = size,
        .client_generation = adapter->generation,
        .bus_generation = 1u
    };
    adapter->active = false;
    adapter->completion_fn(adapter->completion_context, &completion);
    return STORAGE_IO_SUBMIT_ACCEPTED;
}

static StorageIoSubmitResult read_async(
    void *context,
    uint32_t offset,
    uint8_t *buffer,
    uint16_t size,
    StorageOperationToken token,
    uint64_t deadline_us)
{
    (void)deadline_us;
    return complete_operation(context, offset, buffer, NULL, size, token);
}

static StorageIoSubmitResult write_async(
    void *context,
    uint32_t offset,
    const uint8_t *data,
    uint16_t size,
    StorageOperationToken token,
    uint64_t deadline_us)
{
    (void)deadline_us;
    return complete_operation(context, offset, NULL, data, size, token);
}

static void cancel_generation(void *context, uint32_t new_generation)
{
    LinuxStorageAdapter *adapter = context;
    if (!adapter || new_generation == 0u)
        return;
    adapter->generation = new_generation;
    adapter->active = false;
}

static bool is_busy(const void *context)
{
    const LinuxStorageAdapter *adapter = context;
    return adapter && adapter->active;
}

bool storage_port_linux_init(LinuxStorageAdapter *adapter,
                             StoragePort *port_out)
{
    if (!adapter || !port_out)
        return false;
    memset(adapter, 0, sizeof(*adapter));
    adapter->generation = 1u;
    *port_out = (StoragePort){
        .context = adapter,
        .bind_completion = bind_completion,
        .read_async = read_async,
        .write_async = write_async,
        .cancel_generation = cancel_generation,
        .is_busy = is_busy
    };
    return true;
}
