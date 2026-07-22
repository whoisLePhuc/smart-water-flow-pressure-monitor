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
    uint8_t pattern_index;
    uint8_t size_index;
    bool mismatch;
    uint8_t original[64];
    uint8_t expected[64];
    uint8_t actual[64];
} DriverContext;

static DriverContext s_context;

void test_fram_driver_bind_platform(Stm32TestPlatform *platform)
{
    s_context.platform = platform;
}

static void reset(void *context)
{
    DriverContext *test = context;
    Stm32TestPlatform *platform = test->platform;
    memset(test, 0, sizeof(*test));
    test->platform = platform;
    test->fixture_ready = fram_driver_fixture_init(
        &test->fixture, platform, 0x50u);
}

static bool take_completion(DriverContext *test,
                            StorageIoCompletion *completion,
                            bool *ok_out)
{
    if (!fram_driver_fixture_take_completion(&test->fixture, completion))
        return false;
    if (ok_out) {
        *ok_out = completion->result == STORAGE_IO_RESULT_OK &&
                  completion->requested_length ==
                      completion->transferred_length;
    }
    return true;
}

static void fill_pattern(uint8_t *buffer, uint16_t length, uint8_t pattern)
{
    for (uint16_t index = 0u; index < length; ++index) {
        switch (pattern) {
        case 0u: buffer[index] = 0x00u; break;
        case 1u: buffer[index] = 0xffu; break;
        case 2u: buffer[index] = 0xaau; break;
        case 3u: buffer[index] = 0x55u; break;
        default: buffer[index] = (uint8_t)(index * 17u + 3u); break;
        }
    }
}

static Stm32TestResult test_probe_both_blocks(
    Stm32TestRunner *runner, void *context, uint64_t now_us)
{
    DriverContext *test = context;
    StorageIoCompletion completion;
    STM32_TEST_REQUIRE(runner, test->fixture_ready);
    if (test->phase == 0u) {
        STM32_TEST_REQUIRE(runner,
            fram_driver_fixture_probe(&test->fixture, now_us + 500000u) ==
                STORAGE_IO_SUBMIT_ACCEPTED);
        test->phase = 1u;
        return STM32_TEST_RUNNING;
    }
    fram_driver_fixture_poll(&test->fixture, now_us);
    if (!fram_driver_fixture_take_completion(&test->fixture, &completion))
        return STM32_TEST_RUNNING;
    STM32_TEST_REQUIRE(runner, completion.result == STORAGE_IO_RESULT_OK);
    STM32_TEST_REQUIRE(runner, completion.requested_length == 2u);
    STM32_TEST_REQUIRE(runner, completion.transferred_length == 2u);
    STM32_TEST_REQUIRE(runner, test->fixture.driver.completed_count == 1u);
    return STM32_TEST_PASSED;
}

static Stm32TestResult test_reserved_round_trip(
    Stm32TestRunner *runner, void *context, uint64_t now_us)
{
    static const uint16_t sizes[] = {1u, 16u, 31u, 32u, 33u, 64u};
    DriverContext *test = context;
    StorageIoCompletion completion;
    bool completion_ok = false;
    const uint16_t length = sizes[test->size_index];
    STM32_TEST_REQUIRE(runner, test->fixture_ready);

    switch (test->phase) {
    case 0u:
        STM32_TEST_REQUIRE(runner,
            fram_driver_fixture_read(&test->fixture, SLOT_RESERVED_ADDR,
                                     test->original, SLOT_RESERVED_SIZE,
                                     now_us + 500000u) ==
                STORAGE_IO_SUBMIT_ACCEPTED);
        test->phase = 1u;
        break;
    case 1u:
        fram_driver_fixture_poll(&test->fixture, now_us);
        if (!fram_driver_fixture_take_completion(&test->fixture, &completion))
            break;
        STM32_TEST_REQUIRE(runner,
            completion.result == STORAGE_IO_RESULT_OK);
        test->phase = 2u;
        break;
    case 2u:
        fill_pattern(test->expected, length, test->pattern_index);
        memset(test->actual, 0, sizeof(test->actual));
        STM32_TEST_REQUIRE(runner,
            fram_driver_fixture_write(&test->fixture, SLOT_RESERVED_ADDR,
                                      test->expected, length,
                                      now_us + 500000u) ==
                STORAGE_IO_SUBMIT_ACCEPTED);
        test->phase = 3u;
        break;
    case 3u:
        fram_driver_fixture_poll(&test->fixture, now_us);
        if (!take_completion(test, &completion, &completion_ok))
            break;
        STM32_TEST_REQUIRE(runner, completion_ok);
        STM32_TEST_REQUIRE(runner,
            fram_driver_fixture_read(&test->fixture, SLOT_RESERVED_ADDR,
                                     test->actual, length,
                                     now_us + 500000u) ==
                STORAGE_IO_SUBMIT_ACCEPTED);
        test->phase = 4u;
        break;
    case 4u:
        fram_driver_fixture_poll(&test->fixture, now_us);
        if (!take_completion(test, &completion, &completion_ok))
            break;
        STM32_TEST_REQUIRE(runner, completion_ok);
        if (memcmp(test->expected, test->actual, length) != 0)
            test->mismatch = true;
        test->size_index++;
        if (test->size_index >= sizeof(sizes) / sizeof(sizes[0])) {
            test->size_index = 0u;
            test->pattern_index++;
        }
        test->phase = test->pattern_index < 5u ? 2u : 5u;
        break;
    case 5u:
        STM32_TEST_REQUIRE(runner,
            fram_driver_fixture_write(&test->fixture, SLOT_RESERVED_ADDR,
                                      test->original, SLOT_RESERVED_SIZE,
                                      now_us + 500000u) ==
                STORAGE_IO_SUBMIT_ACCEPTED);
        test->phase = 6u;
        break;
    default:
        fram_driver_fixture_poll(&test->fixture, now_us);
        if (!take_completion(test, &completion, &completion_ok))
            break;
        STM32_TEST_REQUIRE(runner, completion_ok);
        if (test->mismatch)
            STM32_TEST_FAIL(runner, "reserved-round-trip-mismatch");
        STM32_TEST_REQUIRE(runner,
            test->fixture.driver.completed_count == 62u);
        return STM32_TEST_PASSED;
    }
    return STM32_TEST_RUNNING;
}

static Stm32TestResult test_cross_block_round_trip(
    Stm32TestRunner *runner, void *context, uint64_t now_us)
{
    enum { START_ADDRESS = 0x0f8u, LENGTH = 40u };
    DriverContext *test = context;
    StorageIoCompletion completion;
    bool completion_ok = false;
    STM32_TEST_REQUIRE(runner, test->fixture_ready);

    switch (test->phase) {
    case 0u:
        STM32_TEST_REQUIRE(runner,
            fram_driver_fixture_read(&test->fixture, START_ADDRESS,
                                     test->original, LENGTH,
                                     now_us + 500000u) ==
                STORAGE_IO_SUBMIT_ACCEPTED);
        test->phase++;
        break;
    case 1u:
        fram_driver_fixture_poll(&test->fixture, now_us);
        if (!take_completion(test, &completion, &completion_ok))
            break;
        STM32_TEST_REQUIRE(runner, completion_ok);
        fill_pattern(test->expected, LENGTH, 4u);
        STM32_TEST_REQUIRE(runner,
            fram_driver_fixture_write(&test->fixture, START_ADDRESS,
                                      test->expected, LENGTH,
                                      now_us + 500000u) ==
                STORAGE_IO_SUBMIT_ACCEPTED);
        test->phase++;
        break;
    case 2u:
        fram_driver_fixture_poll(&test->fixture, now_us);
        if (!take_completion(test, &completion, &completion_ok))
            break;
        STM32_TEST_REQUIRE(runner, completion_ok);
        memset(test->actual, 0, LENGTH);
        STM32_TEST_REQUIRE(runner,
            fram_driver_fixture_read(&test->fixture, START_ADDRESS,
                                     test->actual, LENGTH,
                                     now_us + 500000u) ==
                STORAGE_IO_SUBMIT_ACCEPTED);
        test->phase++;
        break;
    case 3u:
        fram_driver_fixture_poll(&test->fixture, now_us);
        if (!take_completion(test, &completion, &completion_ok))
            break;
        STM32_TEST_REQUIRE(runner, completion_ok);
        test->mismatch = memcmp(test->expected, test->actual, LENGTH) != 0;
        STM32_TEST_REQUIRE(runner,
            fram_driver_fixture_write(&test->fixture, START_ADDRESS,
                                      test->original, LENGTH,
                                      now_us + 500000u) ==
                STORAGE_IO_SUBMIT_ACCEPTED);
        test->phase++;
        break;
    default:
        fram_driver_fixture_poll(&test->fixture, now_us);
        if (!take_completion(test, &completion, &completion_ok))
            break;
        STM32_TEST_REQUIRE(runner, completion_ok);
        if (test->mismatch)
            STM32_TEST_FAIL(runner, "cross-block-mismatch");
        return STM32_TEST_PASSED;
    }
    return STM32_TEST_RUNNING;
}

static const Stm32TestCase s_cases[] = {
    {"IT-FRAM-01", "probe FM24CL04B blocks 0x50 and 0x51", 2000000u,
     false, reset, test_probe_both_blocks, &s_context},
    {"IT-FRAM-02", "reserved-area patterns and chunk boundaries", 15000000u,
     false, reset, test_reserved_round_trip, &s_context},
    {"IT-FRAM-03", "read/write across logical address 0x100", 3000000u,
     true, reset, test_cross_block_round_trip, &s_context}
};

const Stm32TestGroup g_test_fram_driver_group = {
    .name = "integration/storage/fram_driver",
    .cases = s_cases,
    .count = sizeof(s_cases) / sizeof(s_cases[0])
};
