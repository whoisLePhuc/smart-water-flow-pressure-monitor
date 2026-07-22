#ifndef SWFPM_VOLUME_STORAGE_FIXTURE_H
#define SWFPM_VOLUME_STORAGE_FIXTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "protocols/storage/storage_record.h"
#include "support/fram_driver_fixture.h"
#include "support/storage_fixture.h"

typedef struct {
    Stm32TestPlatform *platform;
    FramDriverFixture raw;
    StorageFixture storage;
    uint8_t zero_slot[SLOT_VOLUME_SIZE];
    uint8_t prepare_phase;
    bool initialized;
    bool prepared;
    bool failed;
} VolumeStorageFixture;

bool volume_storage_fixture_init(VolumeStorageFixture *fixture,
                                 Stm32TestPlatform *platform);
bool volume_storage_fixture_poll_prepare(VolumeStorageFixture *fixture,
                                         uint64_t now_us);

#endif /* SWFPM_VOLUME_STORAGE_FIXTURE_H */
