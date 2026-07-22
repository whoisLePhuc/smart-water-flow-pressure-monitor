#include "support/volume_storage_fixture.h"

#include <string.h>

bool volume_storage_fixture_init(VolumeStorageFixture *fixture,
                                 Stm32TestPlatform *platform)
{
    if (!fixture || !platform)
        return false;
    memset(fixture, 0, sizeof(*fixture));
    fixture->platform = platform;
    fixture->initialized = fram_driver_fixture_init(
        &fixture->raw, platform, 0x50u);
    return fixture->initialized;
}

bool volume_storage_fixture_poll_prepare(VolumeStorageFixture *fixture,
                                         uint64_t now_us)
{
    StorageIoCompletion completion;
    if (!fixture || !fixture->initialized || fixture->failed)
        return false;
    if (fixture->prepared)
        return true;

    switch (fixture->prepare_phase) {
    case 0u:
        if (fram_driver_fixture_write(&fixture->raw, SLOT_VOLUME_A_ADDR,
                                      fixture->zero_slot, SLOT_VOLUME_SIZE,
                                      now_us + 500000u) !=
            STORAGE_IO_SUBMIT_ACCEPTED) {
            fixture->failed = true;
            return false;
        }
        fixture->prepare_phase++;
        break;
    case 1u:
        fram_driver_fixture_poll(&fixture->raw, now_us);
        if (!fram_driver_fixture_take_completion(&fixture->raw, &completion))
            break;
        if (completion.result != STORAGE_IO_RESULT_OK) {
            fixture->failed = true;
            break;
        }
        if (fram_driver_fixture_write(&fixture->raw, SLOT_VOLUME_B_ADDR,
                                      fixture->zero_slot, SLOT_VOLUME_SIZE,
                                      now_us + 500000u) !=
            STORAGE_IO_SUBMIT_ACCEPTED) {
            fixture->failed = true;
            break;
        }
        fixture->prepare_phase++;
        break;
    default:
        fram_driver_fixture_poll(&fixture->raw, now_us);
        if (!fram_driver_fixture_take_completion(&fixture->raw, &completion))
            break;
        if (completion.result != STORAGE_IO_RESULT_OK ||
            !storage_fixture_init(&fixture->storage, fixture->platform)) {
            fixture->failed = true;
            break;
        }
        fixture->prepared = true;
        break;
    }
    return fixture->prepared;
}
