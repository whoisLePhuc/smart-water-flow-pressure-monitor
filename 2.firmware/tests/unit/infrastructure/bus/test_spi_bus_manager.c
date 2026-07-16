#include "spi_bus_manager.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t starts;
    uint32_t completions;
    SpiTransactionResult last_result;
} FakeClient;

static bool start(void *context, uint8_t chip_select,
                  const uint8_t *tx, uint8_t *rx, uint16_t length,
                  uint32_t transaction_id, uint32_t correlation_id,
                  uint32_t client_generation, uint32_t bus_generation,
                  uint64_t deadline_us)
{
    FakeClient *fake = context;
    assert(chip_select == 2u && tx && rx && length > 0u);
    assert(transaction_id && correlation_id && client_generation &&
           bus_generation && deadline_us);
    fake->starts++;
    return true;
}

static void complete(void *context,
                     const SpiTransactionCompletion *completion)
{
    FakeClient *fake = context;
    fake->completions++;
    fake->last_result = completion->result;
}

int main(void)
{
    SpiBusManager bus;
    spi_bus_init(&bus);
    FakeClient fake = {0};
    SpiBusClient client = {
        .client_id = 1u, .chip_select = 2u, .client_generation = 3u,
        .context = &fake, .start = start, .complete = complete
    };
    assert(spi_bus_register_client(&bus, &client));
    uint8_t tx[2] = {0}, rx[2] = {0};
    assert(spi_bus_submit(&bus, 1u, 10u, 20u, tx, rx, sizeof(tx),
                          1000u, 1u));
    assert(spi_bus_submit(&bus, 1u, 11u, 21u, tx, rx, sizeof(tx),
                          2000u, 1u));
    assert(fake.starts == 1u && bus.pending_count == 1u);
    assert(!spi_bus_complete(&bus, 10u, 99u, 3u, bus.bus_generation,
                             SPI_TRANSACTION_OK));
    assert(spi_bus_complete(&bus, 10u, 20u, 3u, bus.bus_generation,
                            SPI_TRANSACTION_OK));
    assert(fake.starts == 2u && fake.completions == 1u);
    assert(spi_bus_tick(&bus, 2000u));
    assert(fake.completions == 2u);
    assert(fake.last_result == SPI_TRANSACTION_TIMEOUT);
    puts("SPI Bus Manager Tests: PASS");
    return 0;
}
