#include "test_groups.h"

#include <string.h>

#include "protocols/storage/storage_record.h"
#include "support/stm32_test_assert.h"

typedef struct {
    Stm32TestPlatform *platform;
    uint8_t phase;
    uint8_t tx[2];
    uint8_t rx;
    uint8_t original;
    uint32_t next_transaction_id;
    uint32_t completion_count;
    I2cPortRequest completed_request;
    PortStatus result;
} ContractContext;

static ContractContext s_context;

void test_i2c1_hal_contract_bind_platform(Stm32TestPlatform *platform)
{
    s_context.platform = platform;
}

static void capture(void *context, const I2cPortRequest *request,
                    PortStatus result)
{
    ContractContext *test = context;
    test->completion_count++;
    test->completed_request = *request;
    test->result = result;
}

static void reset(void *context)
{
    ContractContext *test = context;
    Stm32TestPlatform *platform = test->platform;
    memset(test, 0, sizeof(*test));
    test->platform = platform;
    test->next_transaction_id = 1u;
    if (platform) {
        (void)stm32_test_platform_recover(platform);
        stm32_test_platform_set_sink(platform, capture, test);
    }
}

static PortStatus submit(ContractContext *test,
                         uint16_t logical_address,
                         const uint8_t *tx,
                         uint16_t tx_length,
                         uint8_t *rx,
                         uint16_t rx_length,
                         uint64_t deadline_us)
{
    I2cPortRequest request = {
        .transaction_id = test->next_transaction_id++,
        .correlation_id = 1u,
        .client_generation = 1u,
        .bus_generation = 1u,
        .slave_address = (uint8_t)(0x50u |
            ((logical_address >> 8u) & 0x01u)),
        .tx = tx,
        .tx_length = tx_length,
        .rx = rx,
        .rx_length = rx_length,
        .deadline_us = deadline_us
    };
    return test->platform->port.submit(
        test->platform->port.context, &request);
}

static bool take_completion(ContractContext *test, PortStatus *result_out)
{
    stm32_test_platform_poll(test->platform);
    if (test->completion_count == 0u)
        return false;
    if (result_out)
        *result_out = test->result;
    test->completion_count = 0u;
    return true;
}

static Stm32TestResult test_combined_transfer_is_deferred(
    Stm32TestRunner *runner, void *context, uint64_t now_us)
{
    ContractContext *test = context;
    STM32_TEST_REQUIRE(runner, test->platform != NULL);
    if (test->phase == 0u) {
        test->tx[0] = (uint8_t)(SLOT_RESERVED_ADDR & 0xffu);
        STM32_TEST_REQUIRE(runner,
            submit(test, SLOT_RESERVED_ADDR, test->tx, 1u, &test->rx, 1u,
                   now_us + 250000u) == PORT_OK);
        test->phase = 1u;
        return STM32_TEST_RUNNING;
    }

    if (!test->platform->hal.completion_pending)
        return STM32_TEST_RUNNING;

    /* HAL callback has fired, but the portable sink must still be untouched. */
    STM32_TEST_REQUIRE(runner, test->completion_count == 0u);
    stm32_test_platform_poll(test->platform);
    STM32_TEST_REQUIRE(runner, test->completion_count == 1u);
    STM32_TEST_REQUIRE(runner, test->result == PORT_OK);
    STM32_TEST_REQUIRE(runner,
        test->completed_request.transaction_id == 1u);
    stm32_test_platform_poll(test->platform);
    STM32_TEST_REQUIRE(runner, test->completion_count == 1u);
    return STM32_TEST_PASSED;
}

static Stm32TestResult test_tx_and_combined_round_trip(
    Stm32TestRunner *runner, void *context, uint64_t now_us)
{
    ContractContext *test = context;
    PortStatus result;
    STM32_TEST_REQUIRE(runner, test->platform != NULL);

    switch (test->phase) {
    case 0u: /* Save original byte. */
        test->tx[0] = (uint8_t)(SLOT_RESERVED_ADDR & 0xffu);
        STM32_TEST_REQUIRE(runner,
            submit(test, SLOT_RESERVED_ADDR, test->tx, 1u, &test->rx, 1u,
                   now_us + 250000u) == PORT_OK);
        test->phase++;
        break;
    case 1u:
        if (!take_completion(test, &result))
            break;
        STM32_TEST_REQUIRE(runner, result == PORT_OK);
        test->original = test->rx;
        test->tx[0] = (uint8_t)(SLOT_RESERVED_ADDR & 0xffu);
        test->tx[1] = 0xA5u;
        STM32_TEST_REQUIRE(runner,
            submit(test, SLOT_RESERVED_ADDR, test->tx, 2u, NULL, 0u,
                   now_us + 250000u) == PORT_OK);
        test->phase++;
        break;
    case 2u:
        if (!take_completion(test, &result))
            break;
        STM32_TEST_REQUIRE(runner, result == PORT_OK);
        test->tx[0] = (uint8_t)(SLOT_RESERVED_ADDR & 0xffu);
        test->rx = 0u;
        STM32_TEST_REQUIRE(runner,
            submit(test, SLOT_RESERVED_ADDR, test->tx, 1u, &test->rx, 1u,
                   now_us + 250000u) == PORT_OK);
        test->phase++;
        break;
    case 3u:
        if (!take_completion(test, &result))
            break;
        STM32_TEST_REQUIRE(runner, result == PORT_OK);
        STM32_TEST_REQUIRE(runner, test->rx == 0xA5u);
        test->tx[0] = (uint8_t)(SLOT_RESERVED_ADDR & 0xffu);
        test->tx[1] = test->original;
        STM32_TEST_REQUIRE(runner,
            submit(test, SLOT_RESERVED_ADDR, test->tx, 2u, NULL, 0u,
                   now_us + 250000u) == PORT_OK);
        test->phase++;
        break;
    default:
        if (!take_completion(test, &result))
            break;
        STM32_TEST_REQUIRE(runner, result == PORT_OK);
        return STM32_TEST_PASSED;
    }
    return STM32_TEST_RUNNING;
}

static Stm32TestResult test_nack_mapping(
    Stm32TestRunner *runner, void *context, uint64_t now_us)
{
    ContractContext *test = context;
    STM32_TEST_REQUIRE(runner, test->platform != NULL);
    if (test->phase == 0u) {
        I2cPortRequest request = {
            .transaction_id = 77u,
            .correlation_id = 1u,
            .client_generation = 1u,
            .bus_generation = 1u,
            .slave_address = 0x7fu,
            .tx = test->tx,
            .tx_length = 1u,
            .deadline_us = now_us + 250000u
        };
        test->tx[0] = 0u;
        STM32_TEST_REQUIRE(runner,
            test->platform->port.submit(test->platform->port.context,
                                         &request) == PORT_OK);
        test->phase = 1u;
        return STM32_TEST_RUNNING;
    }
    stm32_test_platform_poll(test->platform);
    if (test->completion_count == 0u)
        return STM32_TEST_RUNNING;
    STM32_TEST_REQUIRE(runner,
        test->result == PORT_STATUS_UNAVAILABLE ||
        test->result == PORT_STATUS_HARDWARE_ERROR);
    STM32_TEST_REQUIRE(runner,
        (stm32_i2c1_hal_last_error(&test->platform->hal) &
         HAL_I2C_ERROR_AF) != 0u);
    return STM32_TEST_PASSED;
}

static const Stm32TestCase s_cases[] = {
    {"CT-I2C-01", "TX-then-RX uses deferred exact-once completion",
     1000000u, false, reset, test_combined_transfer_is_deferred, &s_context},
    {"CT-I2C-02", "TX-only write and repeated-start read", 1500000u,
     false, reset, test_tx_and_combined_round_trip, &s_context},
    {"CT-I2C-03", "address NACK maps to portable failure", 1000000u,
     false, reset, test_nack_mapping, &s_context}
};

const Stm32TestGroup g_test_i2c1_hal_contract_group = {
    .name = "contract/platform/i2c1_hal",
    .cases = s_cases,
    .count = sizeof(s_cases) / sizeof(s_cases[0])
};
