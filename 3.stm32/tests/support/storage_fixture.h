#ifndef SWFPM_STORAGE_FIXTURE_H
#define SWFPM_STORAGE_FIXTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "app/app_composition.h"
#include "support/stm32_test_platform.h"

typedef struct {
    Stm32TestPlatform *platform;
    AppComposition app;
    AppCompositionDependencies dependencies;
    uint32_t unmatched_completion_count;
    bool initialized;
} StorageFixture;

bool storage_fixture_init(StorageFixture *fixture,
                          Stm32TestPlatform *platform);
bool storage_fixture_reboot(StorageFixture *fixture);
void storage_fixture_poll(StorageFixture *fixture, uint64_t now_us);

#endif /* SWFPM_STORAGE_FIXTURE_H */
