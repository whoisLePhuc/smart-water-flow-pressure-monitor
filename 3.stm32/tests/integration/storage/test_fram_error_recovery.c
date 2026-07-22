#include "test_groups.h"

#include <string.h>

#include "protocols/storage/storage_record.h"
#include "support/fram_driver_fixture.h"
#include "support/stm32_test_assert.h"

typedef struct {
    Stm32TestPlatform *platform;
    FramDriverFixture fixture;
    bool fixture_ready;
    uint8_t phase;
    uint8_t buffer[64];
} RecoveryContext;

static RecoveryContext s_context;

void test_fram_error_recovery_bind_platform(Stm32TestPlatform *platform)
{
    s_context.platform = platform;
}

static void reset_wrong_address(void *context)
{
    RecoveryContext *test = context;
    Stm32TestPlatform *platform = test->platform;
    memset(test, 0, sizeof(*test));
    test->platform = platform;
    /* 0x56/0x57 is a legal FM24CL04B strapping option, but absent on board. */
    test->fixture_ready = fram_driver_fixture_init(
        &test->fixture, platform, 0x56u);
}

static void reset_normal(void *context)
{
    RecoveryContext *test = context;
    Stm32TestPlatform *platform = test->platform;
    memset(test, 0, sizeof(*test));
    test->platform = platform;
    test->fixture_ready = fram_driver_fixture_init(
        &test->fixture, platform, 0x50u);
}

static Stm32TestResult test_nack_then_reprobe(
    Stm32TestRunner *runner, void *context, uint64_t now_us)
{
    RecoveryContext *test = context;
    StorageIoCompletion completion;
    STM32_TEST_REQUIRE(runner, test->fixture_ready);
    switch (test->phase) {
    case 0u:
        STM32_TEST_REQUIRE(runner,
            fram_driver_fixture_probe(&test->fixture, now_us + 500000u) ==
                STORAGE_IO_SUBMIT_ACCEPTED);
        test->phase++;
        break;
    case 1u:
        fram_driver_fixture_poll(&test->fixture, now_us);
        if (!fram_driver_fixture_take_completion(&test->fixture, &completion))
            break;
        STM32_TEST_REQUIRE(runner,
            completion.result == STORAGE_IO_RESULT_BUS_ERROR);
        STM32_TEST_REQUIRE(runner,
            test->fixture.driver.bus_error_count == 1u);
        test->fixture_ready = fram_driver_fixture_init(
            &test->fixture, test->platform, 0x50u);
        STM32_TEST_REQUIRE(runner, test->fixture_ready);
        STM32_TEST_REQUIRE(runner,
            fram_driver_fixture_probe(&test->fixture, now_us + 500000u) ==
                STORAGE_IO_SUBMIT_ACCEPTED);
        test->phase++;
        break;
    default:
        fram_driver_fixture_poll(&test->fixture, now_us);
        if (!fram_driver_fixture_take_completion(&test->fixture, &completion))
            break;
        STM32_TEST_REQUIRE(runner,
            completion.result == STORAGE_IO_RESULT_OK);
        return STM32_TEST_PASSED;
    }
    return STM32_TEST_RUNNING;
}

static Stm32TestResult test_timeout_exact_once_and_reuse(
    Stm32TestRunner *runner, void *context, uint64_t now_us)
{
    RecoveryContext *test = context;
    StorageIoCompletion completion;
    STM32_TEST_REQUIRE(runner, test->fixture_ready);
    switch (test->phase) {
    case 0u: {
        const uint64_t deadline = now_us + 1u;
        STM32_TEST_REQUIRE(runner,
            fram_driver_fixture_read(&test->fixture, SLOT_RESERVED_ADDR,
                                     test->buffer, sizeof(test->buffer),
                                     deadline) == STORAGE_IO_SUBMIT_ACCEPTED);
        /* Force the manager deadline before deferred HAL completion delivery. */
        STM32_TEST_REQUIRE(runner,
                           i2c_bus_tick(&test->fixture.bus, deadline));
        STM32_TEST_REQUIRE(runner,
            fram_driver_fixture_take_completion(&test->fixture,
                                                &completion));
        STM32_TEST_REQUIRE(runner,
            completion.result == STORAGE_IO_RESULT_TIMEOUT);
        STM32_TEST_REQUIRE(runner,
            test->fixture.driver.timeout_count == 1u);
        STM32_TEST_REQUIRE(runner,
            test->fixture.completion_count == 1u);
        stm32_test_platform_poll(test->platform);
        stm32_test_platform_poll(test->platform);
        STM32_TEST_REQUIRE(runner,
            test->fixture.completion_count == 1u);
        STM32_TEST_REQUIRE(runner,
            fram_driver_fixture_probe(&test->fixture,
                                      now_us + 500000u) ==
                STORAGE_IO_SUBMIT_ACCEPTED);
        test->phase = 1u;
        break;
    }
    default:
        fram_driver_fixture_poll(&test->fixture, now_us);
        if (!fram_driver_fixture_take_completion(&test->fixture, &completion))
            break;
        STM32_TEST_REQUIRE(runner,
            completion.result == STORAGE_IO_RESULT_OK);
        STM32_TEST_REQUIRE(runner,
            test->fixture.completion_count == 2u);
        STM32_TEST_REQUIRE(runner,
            test->fixture.bus.bus_generation >= 2u);
        return STM32_TEST_PASSED;
    }
    return STM32_TEST_RUNNING;
}

static const Stm32TestCase s_cases[] = {
    {"IT-FRAM-04", "NACK then peripheral recovery and reprobe", 3000000u,
     false, reset_wrong_address, test_nack_then_reprobe, &s_context},
    {"IT-FRAM-05", "deadline timeout, late callback and bus reuse", 3000000u,
     false, reset_normal, test_timeout_exact_once_and_reuse, &s_context}
};

const Stm32TestGroup g_test_fram_error_recovery_group = {
    .name = "integration/storage/fram_error_recovery",
    .cases = s_cases,
    .count = sizeof(s_cases) / sizeof(s_cases[0])
};
