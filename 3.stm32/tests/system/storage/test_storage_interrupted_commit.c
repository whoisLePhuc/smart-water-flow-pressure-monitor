#include "test_groups.h"

#include <string.h>

#include "support/stm32_test_assert.h"
#include "support/volume_storage_fixture.h"

typedef struct {
    Stm32TestPlatform *platform;
    VolumeStorageFixture fixture;
    bool fixture_ready;
    uint8_t phase;
    uint8_t encoded[SLOT_VOLUME_SIZE];
} InterruptedContext;

static InterruptedContext s_context;

void test_storage_interrupted_commit_bind_platform(
    Stm32TestPlatform *platform)
{
    s_context.platform = platform;
}

static void reset(void *context)
{
    InterruptedContext *test = context;
    Stm32TestPlatform *platform = test->platform;
    memset(test, 0, sizeof(*test));
    test->platform = platform;
    test->fixture_ready = volume_storage_fixture_init(
        &test->fixture, platform);
}

static bool encode(uint8_t *buffer, uint32_t sequence, uint64_t volume)
{
    return StorageRecord_EncodeVolume(buffer, sequence, volume, 2u,
                                      3u, 4u, volume, sequence, 1u) ==
           SLOT_VOLUME_SIZE;
}

static Stm32TestResult test_reset_before_commit_byte(
    Stm32TestRunner *runner, void *context, uint64_t now_us)
{
    InterruptedContext *test = context;
    StorageCompletionPayload completion;
    StorageRestoreStatus restore_status;
    StorageRestoredVolume restored;
    StorageService *service = &test->fixture.storage.app.storage_service;
    STM32_TEST_REQUIRE(runner, test->fixture_ready);
    STM32_TEST_REQUIRE(runner, !test->fixture.failed);

    switch (test->phase) {
    case 0u:
        if (!volume_storage_fixture_poll_prepare(&test->fixture, now_us))
            break;
        STM32_TEST_REQUIRE(runner, encode(test->encoded, 1u, 1000u));
        STM32_TEST_REQUIRE(runner,
            StorageService_SubmitCheckpoint(service, PERSIST_RECORD_VOLUME,
                1u, test->encoded, SLOT_VOLUME_SIZE, 1000u) == STORAGE_OK);
        test->phase++;
        break;
    case 1u:
        storage_fixture_poll(&test->fixture.storage, now_us);
        if (!StorageService_TakeCompletion(service, &completion))
            break;
        STM32_TEST_REQUIRE(runner,
            completion.status == STORAGE_COMMIT_OK);
        STM32_TEST_REQUIRE(runner, encode(test->encoded, 2u, 2000u));
        STM32_TEST_REQUIRE(runner,
            StorageService_SubmitCheckpoint(service, PERSIST_RECORD_VOLUME,
                2u, test->encoded, SLOT_VOLUME_SIZE, 2000u) == STORAGE_OK);
        test->phase++;
        break;
    case 2u:
        storage_fixture_poll(&test->fixture.storage, now_us);
        if (service->context.state != STORAGE_STATE_COMMIT ||
            service->context.io_pending)
            break;
        /* Discard all RAM state before the final valid commit byte is written. */
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
        STM32_TEST_REQUIRE(runner, restored.forward_volume_ul == 1000u);
        STM32_TEST_REQUIRE(runner, restored.state_version == 1000u);
        STM32_TEST_REQUIRE(runner, restored.last_flow_sequence == 1u);
        return STM32_TEST_PASSED;
    }
    return STM32_TEST_RUNNING;
}

static const Stm32TestCase s_cases[] = {
    {"ST-STOR-02", "interrupted commit retains previous valid A/B slot",
     12000000u, true, reset, test_reset_before_commit_byte, &s_context}
};

const Stm32TestGroup g_test_storage_interrupted_commit_group = {
    .name = "system/storage/interrupted_commit",
    .cases = s_cases,
    .count = sizeof(s_cases) / sizeof(s_cases[0])
};
