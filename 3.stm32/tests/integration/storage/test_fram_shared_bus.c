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
    uint8_t address_byte;
    uint8_t raw_rx;
    bool raw_complete;
    I2cTransactionResult raw_result;
} SharedBusContext;

static SharedBusContext s_context;

void test_fram_shared_bus_bind_platform(Stm32TestPlatform *platform)
{
    s_context.platform = platform;
}

static void raw_completion(
    void *context, const I2cTransactionCompletion *completion)
{
    SharedBusContext *test = context;
    test->raw_complete = true;
    test->raw_result = completion->result;
}

static void reset(void *context)
{
    SharedBusContext *test = context;
    Stm32TestPlatform *platform = test->platform;
    memset(test, 0, sizeof(*test));
    test->platform = platform;
    test->fixture_ready = fram_driver_fixture_init(
        &test->fixture, platform, 0x50u);
}

static Stm32TestResult test_two_clients_are_serialized(
    Stm32TestRunner *runner, void *context, uint64_t now_us)
{
    SharedBusContext *test = context;
    StorageIoCompletion fram_completion;
    STM32_TEST_REQUIRE(runner, test->fixture_ready);
    if (test->phase == 0u) {
        const I2cBusClient raw_client = {
            .client_id = 2u,
            .client_generation = 1u,
            .address_base = 0x50u,
            .address_mask = 0x7eu,
            .context = test,
            .on_complete = raw_completion
        };
        STM32_TEST_REQUIRE(runner,
            i2c_bus_register_client(&test->fixture.bus, &raw_client));
        test->address_byte = (uint8_t)(SLOT_RESERVED_ADDR & 0xffu);
        const I2cBusRequest raw_request = {
            .client_id = 2u,
            .correlation_id = 100u,
            .client_generation = 1u,
            .slave_address = 0x51u,
            .tx = &test->address_byte,
            .tx_length = 1u,
            .rx = &test->raw_rx,
            .rx_length = 1u,
            .deadline_us = now_us + 500000u,
            .priority = 5u
        };
        uint32_t transaction_id = 0u;
        STM32_TEST_REQUIRE(runner,
            i2c_bus_submit(&test->fixture.bus, &raw_request,
                           &transaction_id) == I2C_SUBMIT_ACCEPTED);
        STM32_TEST_REQUIRE(runner,
            fram_driver_fixture_probe(&test->fixture, now_us + 500000u) ==
                STORAGE_IO_SUBMIT_ACCEPTED);
        STM32_TEST_REQUIRE(runner,
            i2c_bus_pending_count(&test->fixture.bus) == 1u);
        STM32_TEST_REQUIRE(runner,
            test->fixture.bus.arbitration_count == 1u);
        test->phase = 1u;
    }

    fram_driver_fixture_poll(&test->fixture, now_us);
    if (!test->raw_complete ||
        !fram_driver_fixture_take_completion(&test->fixture,
                                             &fram_completion))
        return STM32_TEST_RUNNING;
    STM32_TEST_REQUIRE(runner,
        test->raw_result == I2C_TRANSACTION_OK);
    STM32_TEST_REQUIRE(runner,
        fram_completion.result == STORAGE_IO_RESULT_OK);
    STM32_TEST_REQUIRE(runner,
        test->fixture.bus.completed_count == 3u);
    STM32_TEST_REQUIRE(runner, !test->fixture.bus.busy);
    return STM32_TEST_PASSED;
}

static const Stm32TestCase s_cases[] = {
    {"IT-FRAM-06", "shared bus serializes a second client and F-RAM",
     3000000u, false, reset, test_two_clients_are_serialized, &s_context}
};

const Stm32TestGroup g_test_fram_shared_bus_group = {
    .name = "integration/storage/fram_shared_bus",
    .cases = s_cases,
    .count = sizeof(s_cases) / sizeof(s_cases[0])
};
