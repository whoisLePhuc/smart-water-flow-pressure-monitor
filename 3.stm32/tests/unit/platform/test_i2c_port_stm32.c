#include "test_groups.h"

#include <string.h>

#include "platform/stm32/adapters/i2c_port_stm32.h"
#include "support/stm32_test_assert.h"

typedef struct {
    Stm32AsyncHalStatus start_result;
    Stm32AsyncHalStatus cancel_result;
    Stm32AsyncHalStatus recover_result;
    uint32_t start_count;
    uint32_t cancel_count;
    uint32_t recover_count;
    uint32_t completion_count;
    I2cPortRequest completed_request;
    PortStatus completion_status;
    Stm32I2cAdapter adapter;
    I2cPort port;
} UnitContext;

static UnitContext s_context;

static Stm32AsyncHalStatus fake_start(
    void *context, const I2cPortRequest *request)
{
    UnitContext *test = context;
    (void)request;
    test->start_count++;
    return test->start_result;
}

static Stm32AsyncHalStatus fake_cancel(void *context)
{
    UnitContext *test = context;
    test->cancel_count++;
    return test->cancel_result;
}

static Stm32AsyncHalStatus fake_recover(void *context)
{
    UnitContext *test = context;
    test->recover_count++;
    return test->recover_result;
}

static const Stm32I2cHalOps s_ops = {
    .start = fake_start,
    .cancel = fake_cancel,
    .recover = fake_recover
};

static void capture(void *context, const I2cPortRequest *request,
                    PortStatus result)
{
    UnitContext *test = context;
    test->completion_count++;
    test->completed_request = *request;
    test->completion_status = result;
}

static void reset(void *context)
{
    UnitContext *test = context;
    memset(test, 0, sizeof(*test));
    test->start_result = STM32_ASYNC_HAL_OK;
    test->cancel_result = STM32_ASYNC_HAL_OK;
    test->recover_result = STM32_ASYNC_HAL_OK;
}

static bool init_adapter(UnitContext *test)
{
    return i2c_port_stm32_init(&test->adapter, test, &s_ops,
                               capture, test, &test->port) == PORT_OK;
}

static I2cPortRequest request(uint32_t transaction_id,
                              uint32_t bus_generation)
{
    static uint8_t tx = 0u;
    I2cPortRequest value = {0};
    value.transaction_id = transaction_id;
    value.correlation_id = 22u;
    value.client_generation = 1u;
    value.bus_generation = bus_generation;
    value.slave_address = 0x50u;
    value.tx = &tx;
    value.tx_length = 1u;
    value.deadline_us = 1000u;
    return value;
}

static Stm32TestResult test_status_mapping(
    Stm32TestRunner *runner, void *context, uint64_t now_us)
{
    UnitContext *test = context;
    (void)now_us;
    STM32_TEST_REQUIRE(runner,
        i2c_port_stm32_init(NULL, test, &s_ops, capture, test,
                            &test->port) == PORT_STATUS_INVALID_PARAM);
    STM32_TEST_REQUIRE(runner, init_adapter(test));

    I2cPortRequest value = request(1u, 1u);
    test->start_result = STM32_ASYNC_HAL_BUSY;
    STM32_TEST_REQUIRE(runner,
        test->port.submit(test->port.context, &value) == PORT_STATUS_BUSY);
    STM32_TEST_REQUIRE(runner, !test->adapter.active);

    test->start_result = STM32_ASYNC_HAL_ERROR;
    STM32_TEST_REQUIRE(runner,
        test->port.submit(test->port.context, &value) ==
            PORT_STATUS_HARDWARE_ERROR);
    STM32_TEST_REQUIRE(runner, !test->adapter.active);
    return STM32_TEST_PASSED;
}

static Stm32TestResult test_busy_snapshot_exact_once(
    Stm32TestRunner *runner, void *context, uint64_t now_us)
{
    UnitContext *test = context;
    (void)now_us;
    STM32_TEST_REQUIRE(runner, init_adapter(test));
    I2cPortRequest value = request(10u, 4u);
    STM32_TEST_REQUIRE(runner,
        test->port.submit(test->port.context, &value) == PORT_OK);
    value.transaction_id = 99u;
    STM32_TEST_REQUIRE(runner,
        test->port.submit(test->port.context, &value) == PORT_STATUS_BUSY);

    i2c_port_stm32_on_complete(&test->adapter, PORT_OK);
    i2c_port_stm32_on_complete(&test->adapter, PORT_OK);
    STM32_TEST_REQUIRE(runner, test->completion_count == 1u);
    STM32_TEST_REQUIRE(runner,
                       test->completed_request.transaction_id == 10u);
    STM32_TEST_REQUIRE(runner, test->completion_status == PORT_OK);
    STM32_TEST_REQUIRE(runner, !test->adapter.active);
    return STM32_TEST_PASSED;
}

static Stm32TestResult test_cancel_and_recover_identity(
    Stm32TestRunner *runner, void *context, uint64_t now_us)
{
    UnitContext *test = context;
    (void)now_us;
    STM32_TEST_REQUIRE(runner, init_adapter(test));
    I2cPortRequest value = request(7u, 3u);
    STM32_TEST_REQUIRE(runner,
        test->port.submit(test->port.context, &value) == PORT_OK);
    STM32_TEST_REQUIRE(runner,
        test->port.cancel(test->port.context, 8u, 3u) ==
            PORT_STATUS_INVALID_PARAM);
    STM32_TEST_REQUIRE(runner, test->cancel_count == 0u);

    test->cancel_result = STM32_ASYNC_HAL_BUSY;
    STM32_TEST_REQUIRE(runner,
        test->port.cancel(test->port.context, 7u, 3u) == PORT_STATUS_BUSY);
    STM32_TEST_REQUIRE(runner, test->adapter.active);

    test->cancel_result = STM32_ASYNC_HAL_OK;
    STM32_TEST_REQUIRE(runner,
        test->port.cancel(test->port.context, 7u, 3u) == PORT_OK);
    STM32_TEST_REQUIRE(runner, !test->adapter.active);

    value = request(9u, 4u);
    STM32_TEST_REQUIRE(runner,
        test->port.submit(test->port.context, &value) == PORT_OK);
    test->recover_result = STM32_ASYNC_HAL_ERROR;
    STM32_TEST_REQUIRE(runner,
        test->port.recover(test->port.context, 5u) ==
            PORT_STATUS_HARDWARE_ERROR);
    STM32_TEST_REQUIRE(runner, test->adapter.active);
    test->recover_result = STM32_ASYNC_HAL_OK;
    STM32_TEST_REQUIRE(runner,
        test->port.recover(test->port.context, 5u) == PORT_OK);
    STM32_TEST_REQUIRE(runner, !test->adapter.active);
    return STM32_TEST_PASSED;
}

static const Stm32TestCase s_cases[] = {
    {"UT-I2C-01", "HAL submission status mapping", 100000u, false,
     reset, test_status_mapping, &s_context},
    {"UT-I2C-02", "busy, snapshot and exact-once completion", 100000u,
     false, reset, test_busy_snapshot_exact_once, &s_context},
    {"UT-I2C-03", "cancel identity and recovery state", 100000u, false,
     reset, test_cancel_and_recover_identity, &s_context}
};

const Stm32TestGroup g_test_i2c_port_stm32_group = {
    .name = "unit/platform/i2c_port_stm32",
    .cases = s_cases,
    .count = sizeof(s_cases) / sizeof(s_cases[0])
};
