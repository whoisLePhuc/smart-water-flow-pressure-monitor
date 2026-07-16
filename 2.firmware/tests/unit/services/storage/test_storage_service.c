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

    uint64_t forward = 0, reverse = 0, forward_rem = 0, reverse_rem = 0;
    uint64_t version = 0, flow_sequence = 0;
    uint32_t generation = 0;
    assert(StorageService_RestoreVolume(
        &service, &forward, &reverse, &forward_rem, &reverse_rem,
        &version, &flow_sequence, &generation) == STORAGE_RESTORE_OK);
    assert(forward == 1200 && reverse == 30);
    assert(forward_rem == 4 && reverse_rem == 2);
    assert(version == 7 && flow_sequence == 9 && generation == 3);

    puts("Storage Service Tests: PASS");
    return 0;
}
