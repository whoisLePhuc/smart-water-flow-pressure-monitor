#include "i2c_bus_manager.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t submitted[8];
    uint8_t submit_count;
    I2cTransactionCompletion completions[8];
    uint8_t completion_count;
    bool accept;
} FakeClient;

static bool submit(void *ctx, uint8_t addr,
                   const uint8_t *tx, uint16_t tx_len,
                   uint8_t *rx, uint16_t rx_len,
                   uint32_t correlation_id, uint64_t deadline_us)
{
    FakeClient *fake = ctx;
    (void)addr; (void)tx; (void)tx_len; (void)rx; (void)rx_len;
    (void)deadline_us;
    fake->submitted[fake->submit_count++] = correlation_id;
    return fake->accept;
}

static void complete(void *ctx, const I2cTransactionCompletion *completion)
{
    FakeClient *fake = ctx;
    fake->completions[fake->completion_count++] = *completion;
}

int main(void)
{
    I2cBusManager bus;
    FakeClient pressure;
    FakeClient storage;
    memset(&pressure, 0, sizeof(pressure));
    memset(&storage, 0, sizeof(storage));
    pressure.accept = storage.accept = true;
    i2c_bus_init(&bus);

    I2cBusClient p = { 1u, 0x28u, 3u, &pressure, submit, complete };
    I2cBusClient s = { 2u, 0x50u, 1u, &storage, submit, complete };
    assert(i2c_bus_register_client(&bus, &p));
    assert(i2c_bus_register_client(&bus, &s));
    assert(!i2c_bus_register_client(&bus, &s));

    uint8_t byte = 0;
    assert(i2c_bus_submit(&bus, 2u, 10u, 100u, &byte, 1u, NULL, 0u,
                          1000u, 4u));
    assert(i2c_bus_submit(&bus, 2u, 11u, 101u, &byte, 1u, NULL, 0u,
                          1100u, 4u));
    assert(i2c_bus_submit(&bus, 1u, 12u, 102u, &byte, 1u, NULL, 0u,
                          1200u, 1u));
    assert(i2c_bus_pending_count(&bus) == 2u);

    assert(!i2c_bus_complete(&bus, 10u, 999u, 1u, 1u,
                             I2C_TRANSACTION_OK));
    assert(i2c_bus_complete(&bus, 10u, 100u, 1u, 1u,
                            I2C_TRANSACTION_OK));
    assert(i2c_bus_active(&bus)->transaction_id == 12u);
    assert(storage.completion_count == 1u);

    assert(i2c_bus_tick(&bus, 1300u));
    assert(pressure.completions[0].result == I2C_TRANSACTION_TIMEOUT);
    assert(i2c_bus_active(&bus)->transaction_id == 11u);
    assert(i2c_bus_cancel_client(&bus, 2u) == 1u);
    assert(!bus.busy);
    assert(bus.bus_generation == 2u);

    puts("I2C Bus Manager Tests: PASS");
    return 0;
}
