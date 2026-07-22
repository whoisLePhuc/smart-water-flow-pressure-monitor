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
} BootRestoreContext;

static BootRestoreContext s_context;

void test_storage_boot_restore_bind_platform(Stm32TestPlatform *platform)
{
    s_context.platform = platform;
}

static void reset(void *context)
{
    BootRestoreContext *test = context;
    Stm32TestPlatform *platform = test->platform;
    memset(test, 0, sizeof(*test));
    test->platform = platform;
    test->fixture_ready = volume_storage_fixture_init(
        &test->fixture, platform);
}

static Stm32TestResult test_checkpoint_reboot_restore(
    Stm32TestRunner *runner, void *context, uint64_t now_us)
{
    BootRestoreContext *test = context;
    StorageCompletionPayload completion;
    AppComposition *app = &test->fixture.storage.app;
    STM32_TEST_REQUIRE(runner, test->fixture_ready);
    STM32_TEST_REQUIRE(runner, !test->fixture.failed);

    switch (test->phase) {
    case 0u:
        if (!volume_storage_fixture_poll_prepare(&test->fixture, now_us))
            break;
        STM32_TEST_REQUIRE(runner,
            StorageRecord_EncodeVolume(test->encoded, 1u,
                123456789ull, 9876543ull, 11u, 22u, 33u, 44u, 5u) ==
                SLOT_VOLUME_SIZE);
        STM32_TEST_REQUIRE(runner,
            StorageService_SubmitCheckpoint(
                &test->fixture.storage.app.storage_service,
                PERSIST_RECORD_VOLUME, 1u, test->encoded,
                SLOT_VOLUME_SIZE, 33u) == STORAGE_OK);
        test->phase++;
        break;
    case 1u:
        storage_fixture_poll(&test->fixture.storage, now_us);
        if (!StorageService_TakeCompletion(
                &test->fixture.storage.app.storage_service, &completion))
            break;
        STM32_TEST_REQUIRE(runner,
            completion.status == STORAGE_COMMIT_OK);
        STM32_TEST_REQUIRE(runner,
            storage_fixture_reboot(&test->fixture.storage));
        STM32_TEST_REQUIRE(runner,
            app->startup_state == APP_STARTUP_RESTORE_START);
        STM32_TEST_REQUIRE(runner, !app->storage_ready);
        STM32_TEST_REQUIRE(runner, !app->runtime_ready);
        STM32_TEST_REQUIRE(runner,
            app_composition_start(app));
        STM32_TEST_REQUIRE(runner,
            app->startup_state == APP_STARTUP_RESTORE_WAIT);
        STM32_TEST_REQUIRE(runner, !app_composition_start(app));
        test->phase++;
        break;
    default:
        storage_fixture_poll(&test->fixture.storage, now_us);
        STM32_TEST_REQUIRE(runner,
            app->startup_state != APP_STARTUP_RECOVERY_REQUIRED);
        if (app->startup_state == APP_STARTUP_RESTORE_WAIT)
            break;
        STM32_TEST_REQUIRE(runner,
            app->startup_state == APP_STARTUP_STORAGE_READY);
        STM32_TEST_REQUIRE(runner, app->storage_ready);
        STM32_TEST_REQUIRE(runner, !app->runtime_ready);
        STM32_TEST_REQUIRE(runner,
            app->restore_status == STORAGE_RESTORE_OK);
        STM32_TEST_REQUIRE(runner,
            app->restored_volume.forward_volume_ul == 123456789ull);
        STM32_TEST_REQUIRE(runner,
            app->restored_volume.reverse_volume_ul == 9876543ull);
        STM32_TEST_REQUIRE(runner,
            app->restored_volume.forward_remainder == 11u);
        STM32_TEST_REQUIRE(runner,
            app->restored_volume.reverse_remainder == 22u);
        STM32_TEST_REQUIRE(runner,
            app->restored_volume.state_version == 33u);
        STM32_TEST_REQUIRE(runner,
            app->restored_volume.last_flow_sequence == 44u);
        STM32_TEST_REQUIRE(runner,
            app->restored_volume.last_source_generation == 5u);
        STM32_TEST_REQUIRE(runner,
            app->restored_volume.record_sequence == 1u);
        STM32_TEST_REQUIRE(runner,
            app->restored_volume.selected_slot == 0u);
        STM32_TEST_REQUIRE(runner,
            app->restored_volume.slot_a_reason == SLOT_VALID_COMPATIBLE);
        STM32_TEST_REQUIRE(runner,
            app->restored_volume.slot_b_reason == SLOT_EMPTY_UNINITIALIZED);
        STM32_TEST_REQUIRE(runner, app->restore_attempts == 1u);
        STM32_TEST_REQUIRE(runner,
            test->fixture.storage.unmatched_completion_count == 0u);
        {
            StorageRestoreStatus status;
            StorageRestoredVolume volume;
            STM32_TEST_REQUIRE(runner,
                !StorageService_TakeRestoredVolume(
                    &app->storage_service, &status, &volume));
        }
        return STM32_TEST_PASSED;
    }
    return STM32_TEST_RUNNING;
}

static Stm32TestResult test_empty_boot_restore(
    Stm32TestRunner *runner, void *context, uint64_t now_us)
{
    BootRestoreContext *test = context;
    AppComposition *app = &test->fixture.storage.app;
    STM32_TEST_REQUIRE(runner, test->fixture_ready);
    STM32_TEST_REQUIRE(runner, !test->fixture.failed);

    if (!volume_storage_fixture_poll_prepare(&test->fixture, now_us))
        return STM32_TEST_RUNNING;

    if (test->phase == 0u) {
        STM32_TEST_REQUIRE(runner,
            app->startup_state == APP_STARTUP_RESTORE_START);
        STM32_TEST_REQUIRE(runner, app_composition_start(app));
        STM32_TEST_REQUIRE(runner,
            app->startup_state == APP_STARTUP_RESTORE_WAIT);
        test->phase++;
        return STM32_TEST_RUNNING;
    }

    storage_fixture_poll(&test->fixture.storage, now_us);
    STM32_TEST_REQUIRE(runner,
        app->startup_state != APP_STARTUP_RECOVERY_REQUIRED);
    if (app->startup_state == APP_STARTUP_RESTORE_WAIT)
        return STM32_TEST_RUNNING;

    STM32_TEST_REQUIRE(runner,
        app->startup_state == APP_STARTUP_STORAGE_READY);
    STM32_TEST_REQUIRE(runner,
        app->restore_status == STORAGE_RESTORE_EMPTY);
    STM32_TEST_REQUIRE(runner, app->storage_ready);
    STM32_TEST_REQUIRE(runner, !app->runtime_ready);
    STM32_TEST_REQUIRE(runner,
        app->restored_volume.selected_slot == SLOT_INDEX_NONE);
    STM32_TEST_REQUIRE(runner,
        app->restored_volume.slot_a_reason == SLOT_EMPTY_UNINITIALIZED);
    STM32_TEST_REQUIRE(runner,
        app->restored_volume.slot_b_reason == SLOT_EMPTY_UNINITIALIZED);
    return STM32_TEST_PASSED;
}

static const Stm32TestCase s_cases[] = {
    {"ST-STOR-01", "checkpoint, software reboot and volume restore",
     10000000u, true, reset, test_checkpoint_reboot_restore, &s_context},
    {"ST-STOR-04", "canonical empty storage reaches application storage-ready",
     5000000u, true, reset, test_empty_boot_restore, &s_context}
};

const Stm32TestGroup g_test_storage_boot_restore_group = {
    .name = "system/storage/boot_restore",
    .cases = s_cases,
    .count = sizeof(s_cases) / sizeof(s_cases[0])
};
