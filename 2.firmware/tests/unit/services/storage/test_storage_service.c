#include "services/storage/storage_service.h"
#include "storage_port_linux.h"

#include <assert.h>
#include <stdio.h>

static void run_until_terminal(StorageService *service, uint64_t *now_us)
{
    for (unsigned i = 0u; i < 128u; ++i) {
        StorageServiceState state = service->context.state;
        if (state == STORAGE_STATE_COMPLETE ||
            state == STORAGE_STATE_FAILED ||
            state == STORAGE_STATE_RESTORE_COMPLETE ||
            state == STORAGE_STATE_RESTORE_FAILED)
            return;
        StorageService_Tick(service, *now_us);
        *now_us += 100u;
    }
    assert(false && "storage service did not reach a terminal state");
}

int main(void)
{
    LinuxStorageAdapter adapter;
    StoragePort port;
    StorageService service;
    uint8_t record[SLOT_VOLUME_SIZE];
    uint64_t now_us = 1000u;

    assert(storage_port_linux_init(&adapter, &port));
    assert(StorageService_Init(&service, &port, 50000u) == STORAGE_OK);

    assert(StorageRecord_EncodeVolume(record, 1u, 1200u, 30u, 4u, 2u,
                                      7u, 9u, 3u) == SLOT_VOLUME_SIZE);
    assert(StorageService_SubmitCheckpoint(
        &service, PERSIST_RECORD_VOLUME, 1u, record,
        SLOT_VOLUME_SIZE, 7u) == STORAGE_OK);
    run_until_terminal(&service, &now_us);
    assert(service.context.state == STORAGE_STATE_COMPLETE);
    assert(service.context.target_slot == 0u);

    StorageCompletionPayload completion;
    assert(StorageService_TakeCompletion(&service, &completion));
    assert(completion.status == STORAGE_COMMIT_OK);
    assert(completion.selected_slot == 0u);

    assert(StorageRecord_EncodeVolume(record, 2u, 2400u, 60u, 8u, 4u,
                                      8u, 10u, 3u) == SLOT_VOLUME_SIZE);
    assert(StorageService_SubmitCheckpoint(
        &service, PERSIST_RECORD_VOLUME, 2u, record,
        SLOT_VOLUME_SIZE, 8u) == STORAGE_OK);
    run_until_terminal(&service, &now_us);
    assert(service.context.state == STORAGE_STATE_COMPLETE);
    assert(service.context.target_slot == 1u);
    assert(adapter.memory[SLOT_VOLUME_A_ADDR + SLOT_VOLUME_SIZE - 1u] ==
           PERSIST_COMMIT_VALID);
    assert(adapter.memory[SLOT_VOLUME_B_ADDR + SLOT_VOLUME_SIZE - 1u] ==
           PERSIST_COMMIT_VALID);
    assert(StorageService_TakeCompletion(&service, &completion));
    assert(completion.status == STORAGE_COMMIT_OK);
    assert(completion.selected_slot == 1u);

    assert(StorageService_StartRestoreVolume(&service) == STORAGE_OK);
    run_until_terminal(&service, &now_us);
    StorageRestoreStatus restore_status;
    StorageRestoredVolume restored;
    assert(StorageService_TakeRestoredVolume(
        &service, &restore_status, &restored));
    assert(restore_status == STORAGE_RESTORE_OK);
    assert(restored.forward_volume_ul == 2400u);
    assert(restored.reverse_volume_ul == 60u);
    assert(restored.forward_remainder == 8u);
    assert(restored.reverse_remainder == 4u);
    assert(restored.state_version == 8u);
    assert(restored.last_flow_sequence == 10u);
    assert(restored.last_source_generation == 3u);

    puts("Storage Service Tests: PASS");
    return 0;
}
