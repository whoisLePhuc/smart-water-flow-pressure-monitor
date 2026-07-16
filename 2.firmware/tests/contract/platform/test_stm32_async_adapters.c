#include "i2c_port_stm32.h"
#include "spi_port_stm32.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t starts;
    uint32_t completions;
    uint32_t last_transaction;
} FakeHal;

static Stm32AsyncHalStatus i2c_start(void *context,
                                     const I2cPortRequest *request)
{
    FakeHal *fake = context;
    fake->starts++;
    fake->last_transaction = request->transaction_id;
    return STM32_ASYNC_HAL_OK;
}
static Stm32AsyncHalStatus i2c_ok(void *context)
{
    (void)context;
    return STM32_ASYNC_HAL_OK;
}
static void i2c_sink(void *context, const I2cPortRequest *request,
                     PortStatus status)
{
    FakeHal *fake = context;
    assert(status == PORT_OK);
    fake->completions++;
    fake->last_transaction = request->transaction_id;
}

static PortStatus spi_start(void *context, const SpiPortRequest *request)
{
    FakeHal *fake = context;
    fake->starts++;
    fake->last_transaction = request->transaction_id;
    return PORT_OK;
}
static PortStatus spi_ok(void *context)
{
    (void)context;
    return PORT_OK;
}
static void spi_sink(void *context, const SpiPortRequest *request,
                     PortStatus status)
{
    FakeHal *fake = context;
    assert(status == PORT_OK);
    fake->completions++;
    fake->last_transaction = request->transaction_id;
}

int main(void)
{
    FakeHal i2c_fake = {0};
    Stm32I2cHalOps i2c_ops = {
        .start = i2c_start, .cancel = i2c_ok, .recover = i2c_ok
    };
    Stm32I2cAdapter i2c_adapter;
    I2cPort i2c_port;
    assert(i2c_port_stm32_init(&i2c_adapter, &i2c_fake, &i2c_ops,
                               i2c_sink, &i2c_fake, &i2c_port) == PORT_OK);
    I2cPortRequest i2c_request;
    memset(&i2c_request, 0, sizeof(i2c_request));
    i2c_request.transaction_id = 10u;
    assert(i2c_port.submit(i2c_port.context, &i2c_request) == PORT_OK);
    assert(i2c_port.submit(i2c_port.context, &i2c_request) ==
           PORT_STATUS_BUSY);
    i2c_port_stm32_on_complete(&i2c_adapter, PORT_OK);
    assert(i2c_fake.starts == 1u && i2c_fake.completions == 1u);

    FakeHal spi_fake = {0};
    Stm32SpiHalOps spi_ops = {
        .start = spi_start, .cancel = spi_ok, .recover = spi_ok
    };
    Stm32SpiAdapter spi_adapter;
    SpiPort spi_port;
    assert(spi_port_stm32_init(&spi_adapter, &spi_fake, &spi_ops,
                               spi_sink, &spi_fake, &spi_port) == PORT_OK);
    SpiPortRequest spi_request;
    memset(&spi_request, 0, sizeof(spi_request));
    spi_request.transaction_id = 20u;
    assert(spi_port.submit(spi_port.context, &spi_request) == PORT_OK);
    spi_port_stm32_on_complete(&spi_adapter, PORT_OK);
    assert(spi_fake.starts == 1u && spi_fake.completions == 1u);

    puts("STM32 Async Adapter Contract Tests: PASS");
    return 0;
}
