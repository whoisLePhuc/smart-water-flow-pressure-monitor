#include "services/storage/storage_service.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    FramDriver fram;
    StorageService service;
    uint8_t record[SLOT_VOLUME_SIZE];
    FramDriver_Init(&fram, false, NULL, 0x50);
    assert(StorageService_Init(&service, &fram) == STORAGE_OK);
    assert(service.fram == &fram);

    assert(StorageRecord_EncodeVolume(record, 1, 1200, 30, 4, 2,
                                      7, 9, 3) == SLOT_VOLUME_SIZE);
    assert(StorageService_SubmitCheckpoint(
        &service, PERSIST_RECORD_VOLUME, 1, record,
        SLOT_VOLUME_SIZE, 7) == STORAGE_OK);

    for (unsigned i = 0; i < 16
         && service.context.state != STORAGE_STATE_COMPLETE; ++i)
        StorageService_Tick(&service);
    assert(service.context.state == STORAGE_STATE_COMPLETE);
    assert(service.context.target_slot == 0u);

    assert(StorageRecord_EncodeVolume(record, 2, 2400, 60, 8, 4,
                                      8, 10, 3) == SLOT_VOLUME_SIZE);
    assert(StorageService_SubmitCheckpoint(
        &service, PERSIST_RECORD_VOLUME, 2, record,
        SLOT_VOLUME_SIZE, 8) == STORAGE_OK);
    for (unsigned i = 0; i < 16
         && service.context.state != STORAGE_STATE_COMPLETE; ++i)
        StorageService_Tick(&service);
    assert(service.context.state == STORAGE_STATE_COMPLETE);
    assert(service.context.target_slot == 1u);
    assert(fram.memory[SLOT_VOLUME_A_ADDR + SLOT_VOLUME_SIZE - 1u] ==
           PERSIST_COMMIT_VALID);
    assert(fram.memory[SLOT_VOLUME_B_ADDR + SLOT_VOLUME_SIZE - 1u] ==
           PERSIST_COMMIT_VALID);

    uint64_t forward = 0, reverse = 0, forward_rem = 0, reverse_rem = 0;
    uint64_t version = 0, flow_sequence = 0;
    uint32_t generation = 0;
    assert(StorageService_RestoreVolume(
        &service, &forward, &reverse, &forward_rem, &reverse_rem,
        &version, &flow_sequence, &generation) == STORAGE_RESTORE_OK);
    assert(forward == 2400 && reverse == 60);
    assert(forward_rem == 8 && reverse_rem == 4);
    assert(version == 8 && flow_sequence == 10 && generation == 3);

    puts("Storage Service Tests: PASS");
    return 0;
}
