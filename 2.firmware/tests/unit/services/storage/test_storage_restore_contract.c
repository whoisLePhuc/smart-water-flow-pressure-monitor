#include "services/storage/storage_service.h"
#include "storage_port_linux.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    StorageIoCompletionFn completion_fn;
    void *completion_context;
} FailingStorageAdapter;

static bool failing_bind(void *context,
                         StorageIoCompletionFn completion_fn,
                         void *completion_context)
{
    FailingStorageAdapter *adapter = context;
    adapter->completion_fn = completion_fn;
    adapter->completion_context = completion_context;
    return true;
}

static StorageIoSubmitResult failing_read(
    void *context, uint32_t offset, uint8_t *buffer, uint16_t size,
    StorageOperationToken token, uint64_t deadline_us)
{
    FailingStorageAdapter *adapter = context;
    (void)offset;
    (void)buffer;
    (void)deadline_us;
    StorageIoCompletion completion = {
        .token = token,
        .result = STORAGE_IO_RESULT_BUS_ERROR,
        .requested_length = size,
        .transferred_length = 0u,
        .client_generation = 1u,
        .bus_generation = 1u
    };
    adapter->completion_fn(adapter->completion_context, &completion);
    return STORAGE_IO_SUBMIT_ACCEPTED;
}

static StorageIoSubmitResult failing_write(
    void *context, uint32_t offset, const uint8_t *buffer, uint16_t size,
    StorageOperationToken token, uint64_t deadline_us)
{
    return failing_read(context, offset, (uint8_t *)buffer, size,
                        token, deadline_us);
}

static void failing_cancel(void *context, uint32_t new_generation)
{
    (void)context;
    (void)new_generation;
}

static bool failing_is_busy(const void *context)
{
    (void)context;
    return false;
}

static void write_le32(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8u);
    buffer[2] = (uint8_t)(value >> 16u);
    buffer[3] = (uint8_t)(value >> 24u);
}

static void refresh_crc(uint8_t *slot)
{
    write_le32(slot + 0x0Cu,
               StorageRecord_ComputeCrc(slot, SLOT_VOLUME_SIZE));
}

static void make_valid_slot(uint8_t *slot, uint32_t sequence,
                            uint64_t forward_volume)
{
    assert(StorageRecord_EncodeVolume(
        slot, sequence, forward_volume, 0u, 0u, 0u,
        sequence, sequence, 1u) == SLOT_VOLUME_SIZE);
    slot[SLOT_VOLUME_SIZE - 1u] = PERSIST_COMMIT_VALID;
}

static void run_until_terminal(StorageService *service, uint64_t *now_us)
{
    for (unsigned i = 0u; i < 64u; ++i) {
        if (service->context.state == STORAGE_STATE_COMPLETE ||
            service->context.state == STORAGE_STATE_FAILED ||
            service->context.state == STORAGE_STATE_RESTORE_COMPLETE ||
            service->context.state == STORAGE_STATE_RESTORE_FAILED)
            return;
        StorageService_Tick(service, *now_us);
        *now_us += 100u;
    }
    assert(false && "restore did not reach a terminal state");
}

static StorageRestoreStatus restore_image(
    const uint8_t memory[LINUX_STORAGE_CAPACITY_BYTES],
    StorageRestoredVolume *restored)
{
    LinuxStorageAdapter adapter;
    StoragePort port;
    StorageService service;
    uint64_t now_us = 1000u;
    assert(storage_port_linux_init(&adapter, &port));
    memcpy(adapter.memory, memory, sizeof(adapter.memory));
    assert(StorageService_Init(&service, &port, 50000u) == STORAGE_OK);
    assert(StorageService_StartRestoreVolume(&service) == STORAGE_OK);
    run_until_terminal(&service, &now_us);

    StorageRestoreStatus status = STORAGE_RESTORE_INTERNAL_ERROR;
    assert(StorageService_TakeRestoredVolume(
        &service, &status, restored));
    return status;
}

static void test_both_canonical_empty(void)
{
    uint8_t memory[LINUX_STORAGE_CAPACITY_BYTES] = {0};
    StorageRestoredVolume restored;
    assert(restore_image(memory, &restored) == STORAGE_RESTORE_EMPTY);
    assert(restored.selected_slot == SLOT_INDEX_NONE);
    assert(restored.slot_a_reason == SLOT_EMPTY_UNINITIALIZED);
    assert(restored.slot_b_reason == SLOT_EMPTY_UNINITIALIZED);
}

static void test_noncanonical_blank_is_corrupt(void)
{
    uint8_t memory[LINUX_STORAGE_CAPACITY_BYTES];
    memset(memory, 0xFF, sizeof(memory));
    StorageRestoredVolume restored;
    assert(restore_image(memory, &restored) == STORAGE_RESTORE_CORRUPT);
    assert(restored.slot_a_reason == SLOT_NOT_COMMITTED);
    assert(restored.slot_b_reason == SLOT_NOT_COMMITTED);
}

static void test_both_crc_bad_are_corrupt(void)
{
    uint8_t memory[LINUX_STORAGE_CAPACITY_BYTES] = {0};
    make_valid_slot(memory + SLOT_VOLUME_A_ADDR, 1u, 100u);
    make_valid_slot(memory + SLOT_VOLUME_B_ADDR, 2u, 200u);
    memory[SLOT_VOLUME_A_ADDR + 0x10u] ^= 1u;
    memory[SLOT_VOLUME_B_ADDR + 0x10u] ^= 1u;

    StorageRestoredVolume restored;
    assert(restore_image(memory, &restored) == STORAGE_RESTORE_CORRUPT);
    assert(restored.slot_a_reason == SLOT_BAD_CRC);
    assert(restored.slot_b_reason == SLOT_BAD_CRC);
}

static void test_future_schema_is_unsupported(void)
{
    uint8_t memory[LINUX_STORAGE_CAPACITY_BYTES] = {0};
    uint8_t *slot = memory + SLOT_VOLUME_A_ADDR;
    make_valid_slot(slot, 3u, 300u);
    slot[5] = 2u;
    refresh_crc(slot);

    StorageRestoredVolume restored;
    assert(restore_image(memory, &restored) ==
           STORAGE_RESTORE_UNSUPPORTED_SCHEMA);
    assert(restored.slot_a_reason == SLOT_VALID_UNSUPPORTED_SCHEMA);
    assert(restored.slot_b_reason == SLOT_EMPTY_UNINITIALIZED);
}

static void test_older_compatible_slot_wins_without_hiding_future(void)
{
    uint8_t memory[LINUX_STORAGE_CAPACITY_BYTES] = {0};
    make_valid_slot(memory + SLOT_VOLUME_A_ADDR, 3u, 300u);
    uint8_t *future = memory + SLOT_VOLUME_B_ADDR;
    make_valid_slot(future, 4u, 400u);
    future[5] = 2u;
    refresh_crc(future);

    StorageRestoredVolume restored;
    assert(restore_image(memory, &restored) == STORAGE_RESTORE_OK);
    assert(restored.record_sequence == 3u);
    assert(restored.selected_slot == 0u);
    assert(restored.slot_a_reason == SLOT_VALID_COMPATIBLE);
    assert(restored.slot_b_reason == SLOT_VALID_UNSUPPORTED_SCHEMA);
}

static void test_equal_different_is_sequence_conflict(void)
{
    uint8_t memory[LINUX_STORAGE_CAPACITY_BYTES] = {0};
    make_valid_slot(memory + SLOT_VOLUME_A_ADDR, 4u, 400u);
    make_valid_slot(memory + SLOT_VOLUME_B_ADDR, 4u, 401u);

    StorageRestoredVolume restored;
    assert(restore_image(memory, &restored) ==
           STORAGE_RESTORE_SEQUENCE_CONFLICT);
    assert(restored.selected_slot == SLOT_INDEX_NONE);
}

static void test_half_range_is_sequence_conflict(void)
{
    uint8_t memory[LINUX_STORAGE_CAPACITY_BYTES] = {0};
    make_valid_slot(memory + SLOT_VOLUME_A_ADDR, 0u, 100u);
    make_valid_slot(memory + SLOT_VOLUME_B_ADDR, 0x80000000u, 200u);

    StorageRestoredVolume restored;
    assert(restore_image(memory, &restored) ==
           STORAGE_RESTORE_SEQUENCE_CONFLICT);
}

static void test_valid_slot_wins_with_degraded_evidence(void)
{
    uint8_t memory[LINUX_STORAGE_CAPACITY_BYTES] = {0};
    make_valid_slot(memory + SLOT_VOLUME_A_ADDR, 7u, 700u);
    make_valid_slot(memory + SLOT_VOLUME_B_ADDR, 8u, 800u);
    memory[SLOT_VOLUME_B_ADDR + 0x10u] ^= 1u;

    StorageRestoredVolume restored;
    assert(restore_image(memory, &restored) == STORAGE_RESTORE_OK);
    assert(restored.forward_volume_ul == 700u);
    assert(restored.record_sequence == 7u);
    assert(restored.selected_slot == 0u);
    assert(restored.slot_a_reason == SLOT_VALID_COMPATIBLE);
    assert(restored.slot_b_reason == SLOT_BAD_CRC);
}

static void test_commit_preserves_future_schema_slot(void)
{
    LinuxStorageAdapter adapter;
    StoragePort port;
    StorageService service;
    uint64_t now_us = 1000u;
    uint8_t candidate[SLOT_VOLUME_SIZE];
    uint8_t future_before[SLOT_VOLUME_SIZE];

    assert(storage_port_linux_init(&adapter, &port));
    make_valid_slot(adapter.memory + SLOT_VOLUME_A_ADDR, 3u, 300u);
    uint8_t *future = adapter.memory + SLOT_VOLUME_B_ADDR;
    make_valid_slot(future, 4u, 400u);
    future[5] = 2u;
    refresh_crc(future);
    memcpy(future_before, future, sizeof(future_before));

    assert(StorageService_Init(&service, &port, 50000u) == STORAGE_OK);
    assert(StorageRecord_EncodeVolume(candidate, 4u, 350u, 0u, 0u, 0u,
                                      4u, 4u, 1u) == SLOT_VOLUME_SIZE);
    assert(StorageService_SubmitCheckpoint(
        &service, PERSIST_RECORD_VOLUME, 4u, candidate,
        SLOT_VOLUME_SIZE, 4u) == STORAGE_OK);
    run_until_terminal(&service, &now_us);

    StorageCompletionPayload completion;
    assert(StorageService_TakeCompletion(&service, &completion));
    assert(completion.status == STORAGE_COMMIT_REJECTED);
    assert(completion.selected_slot == SLOT_INDEX_NONE);
    assert(memcmp(future, future_before, sizeof(future_before)) == 0);
}

static void test_commit_rejects_sequence_conflict(void)
{
    LinuxStorageAdapter adapter;
    StoragePort port;
    StorageService service;
    uint64_t now_us = 1000u;
    uint8_t candidate[SLOT_VOLUME_SIZE];
    uint8_t slots_before[SLOT_VOLUME_SIZE * 2u];

    assert(storage_port_linux_init(&adapter, &port));
    make_valid_slot(adapter.memory + SLOT_VOLUME_A_ADDR, 5u, 500u);
    make_valid_slot(adapter.memory + SLOT_VOLUME_B_ADDR, 5u, 501u);
    memcpy(slots_before, adapter.memory + SLOT_VOLUME_A_ADDR,
           sizeof(slots_before));

    assert(StorageService_Init(&service, &port, 50000u) == STORAGE_OK);
    assert(StorageRecord_EncodeVolume(candidate, 6u, 600u, 0u, 0u, 0u,
                                      6u, 6u, 1u) == SLOT_VOLUME_SIZE);
    assert(StorageService_SubmitCheckpoint(
        &service, PERSIST_RECORD_VOLUME, 6u, candidate,
        SLOT_VOLUME_SIZE, 6u) == STORAGE_OK);
    run_until_terminal(&service, &now_us);

    StorageCompletionPayload completion;
    assert(StorageService_TakeCompletion(&service, &completion));
    assert(completion.status == STORAGE_COMMIT_SEQUENCE_CONFLICT);
    assert(completion.selected_slot == SLOT_INDEX_NONE);
    assert(memcmp(adapter.memory + SLOT_VOLUME_A_ADDR, slots_before,
                  sizeof(slots_before)) == 0);
}

static void test_io_error_returns_no_stale_volume(void)
{
    FailingStorageAdapter adapter = {0};
    StoragePort port = {
        .context = &adapter,
        .bind_completion = failing_bind,
        .read_async = failing_read,
        .write_async = failing_write,
        .cancel_generation = failing_cancel,
        .is_busy = failing_is_busy
    };
    StorageService service;
    uint64_t now_us = 1000u;
    assert(StorageService_Init(&service, &port, 50000u) == STORAGE_OK);

    service.restored_volume.forward_volume_ul = 999u;
    service.restored_volume.record_sequence = 77u;
    assert(StorageService_StartRestoreVolume(&service) == STORAGE_OK);
    run_until_terminal(&service, &now_us);

    StorageRestoreStatus status;
    StorageRestoredVolume restored;
    assert(StorageService_TakeRestoredVolume(&service, &status, &restored));
    assert(status == STORAGE_RESTORE_IO_ERROR);
    assert(restored.forward_volume_ul == 0u);
    assert(restored.record_sequence == 0u);
    assert(restored.selected_slot == SLOT_INDEX_NONE);
    assert(restored.slot_a_reason == SLOT_IO_ERROR);
    assert(restored.slot_b_reason == SLOT_IO_ERROR);
}

int main(void)
{
    test_both_canonical_empty();
    test_noncanonical_blank_is_corrupt();
    test_both_crc_bad_are_corrupt();
    test_future_schema_is_unsupported();
    test_older_compatible_slot_wins_without_hiding_future();
    test_equal_different_is_sequence_conflict();
    test_half_range_is_sequence_conflict();
    test_valid_slot_wins_with_degraded_evidence();
    test_commit_preserves_future_schema_slot();
    test_commit_rejects_sequence_conflict();
    test_io_error_returns_no_stale_volume();
    puts("Storage Restore Contract Tests: PASS");
    return 0;
}
