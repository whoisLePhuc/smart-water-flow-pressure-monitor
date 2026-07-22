#include "test_groups.h"

#include <string.h>

#include "support/stm32_test_assert.h"
#include "support/volume_storage_fixture.h"

#ifndef SWFPM_FRAM_SOAK_ITERATIONS
#define SWFPM_FRAM_SOAK_ITERATIONS 100u
#endif

typedef struct {
    Stm32TestPlatform *platform;
    VolumeStorageFixture fixture;
    bool fixture_ready;
    uint8_t phase;
    uint32_t iteration;
    uint8_t encoded[SLOT_VOLUME_SIZE];
} SoakContext;

static SoakContext s_context;

void test_storage_soak_bind_platform(Stm32TestPlatform *platform)
{
    s_context.platform = platform;
}

static void reset(void *context)
{
    SoakContext *test = context;
    Stm32TestPlatform *platform = test->platform;
    memset(test, 0, sizeof(*test));
    test->platform = platform;
    test->fixture_ready = volume_storage_fixture_init(
        &test->fixture, platform);
}

static Stm32TestResult test_checkpoint_soak(
    Stm32TestRunner *runner, void *context, uint64_t now_us)
{
    SoakContext *test = context;
    StorageService *service = &test->fixture.storage.app.storage_service;
    StorageCompletionPayload completion;
    StorageRestoreStatus restore_status;
    StorageRestoredVolume restored;
    STM32_TEST_REQUIRE(runner, test->fixture_ready);
    STM32_TEST_REQUIRE(runner, !test->fixture.failed);

    switch (test->phase) {
    case 0u:
        if (!volume_storage_fixture_poll_prepare(&test->fixture, now_us))
            break;
        test->iteration = 1u;
        test->phase++;
        break;
    case 1u:
        STM32_TEST_REQUIRE(runner,
            StorageRecord_EncodeVolume(
                test->encoded, test->iteration,
                (uint64_t)test->iteration * 1000003ull,
                (uint64_t)test->iteration * 17ull,
                test->iteration + 1u, test->iteration + 2u,
                test->iteration, test->iteration * 10ull, 1u) ==
                SLOT_VOLUME_SIZE);
        STM32_TEST_REQUIRE(runner,
            StorageService_SubmitCheckpoint(
                service, PERSIST_RECORD_VOLUME, test->iteration,
                test->encoded, SLOT_VOLUME_SIZE, test->iteration) ==
                STORAGE_OK);
        test->phase++;
        break;
    case 2u:
        storage_fixture_poll(&test->fixture.storage, now_us);
        if (!StorageService_TakeCompletion(service, &completion))
            break;
        STM32_TEST_REQUIRE(runner,
            completion.status == STORAGE_COMMIT_OK);
        STM32_TEST_REQUIRE(runner,
            completion.record_sequence == test->iteration);
        if (test->iteration < SWFPM_FRAM_SOAK_ITERATIONS) {
            test->iteration++;
            test->phase = 1u;
            break;
        }
        STM32_TEST_REQUIRE(runner,
            storage_fixture_reboot(&test->fixture.storage));
        service = &test->fixture.storage.app.storage_service;
        STM32_TEST_REQUIRE(runner,
            StorageService_StartRestoreVolume(service) == STORAGE_OK);
        test->phase++;
        break;
    default:
        storage_fixture_poll(&test->fixture.storage, now_us);
        service = &test->fixture.storage.app.storage_service;
        if (!StorageService_TakeRestoredVolume(
                service, &restore_status, &restored))
            break;
        STM32_TEST_REQUIRE(runner,
            restore_status == STORAGE_RESTORE_OK);
        STM32_TEST_REQUIRE(runner,
            restored.forward_volume_ul ==
                (uint64_t)SWFPM_FRAM_SOAK_ITERATIONS * 1000003ull);
        STM32_TEST_REQUIRE(runner,
            restored.last_flow_sequence ==
                (uint64_t)SWFPM_FRAM_SOAK_ITERATIONS * 10ull);
        STM32_TEST_REQUIRE(runner,
            restored.state_version == SWFPM_FRAM_SOAK_ITERATIONS);
        return STM32_TEST_PASSED;
    }
    return STM32_TEST_RUNNING;
}

static const Stm32TestCase s_cases[] = {
    {"ST-STOR-03", "repeated A/B checkpoints and final restore", 180000000u,
     true, reset, test_checkpoint_soak, &s_context}
};

const Stm32TestGroup g_test_storage_soak_group = {
    .name = "system/storage/soak",
    .cases = s_cases,
    .count = sizeof(s_cases) / sizeof(s_cases[0])
};
