#include "support/fram_driver_fixture.h"

#include <string.h>

static void route_bus_completion(void *context,
                                 const I2cPortRequest *request,
                                 PortStatus result)
{
    FramDriverFixture *fixture = context;
    if (fixture)
        (void)i2c_bus_on_port_completion(&fixture->bus, request, result);
}

static void capture_storage_completion(
    void *context, const StorageIoCompletion *completion)
{
    FramDriverFixture *fixture = context;
    if (!fixture || !completion)
        return;
    fixture->completion = *completion;
    fixture->completion_count++;
    fixture->completion_ready = true;
}

bool fram_driver_fixture_init(FramDriverFixture *fixture,
                              Stm32TestPlatform *platform,
                              uint8_t base_address_7bit)
{
    if (!fixture || !platform || !platform->initialized)
        return false;
    (void)stm32_test_platform_recover(platform);
    memset(fixture, 0, sizeof(*fixture));
    fixture->platform = platform;
    fixture->next_operation_id = 1u;
    fixture->next_correlation_id = 1u;

    stm32_test_platform_set_sink(platform, route_bus_completion, fixture);
    i2c_bus_init(&fixture->bus, &platform->port);

    const FramConfig config = {
        .client_id = 1u,
        .slave_address_base_7bit = base_address_7bit,
        .capacity_bytes = FM24CL04B_SIZE_BYTES,
        .max_chunk_bytes = FM24CL04B_MAX_CHUNK_BYTES,
        .bus_priority = 3u
    };
    if (fram_init(&fixture->driver, &fixture->bus, &config) !=
        STORAGE_IO_SUBMIT_ACCEPTED)
        return false;
    if (!fram_make_storage_port(&fixture->driver, &fixture->port))
        return false;
    if (!fixture->port.bind_completion(fixture->port.context,
                                       capture_storage_completion, fixture))
        return false;
    fixture->initialized = true;
    return true;
}

void fram_driver_fixture_poll(FramDriverFixture *fixture, uint64_t now_us)
{
    if (!fixture || !fixture->initialized)
        return;
    stm32_test_platform_poll(fixture->platform);
    (void)i2c_bus_tick(&fixture->bus, now_us);
}

StorageOperationToken fram_driver_fixture_token(FramDriverFixture *fixture)
{
    StorageOperationToken token = {0};
    if (!fixture)
        return token;
    token.operation_id = fixture->next_operation_id++;
    token.correlation_id = fixture->next_correlation_id++;
    token.owner_generation = fixture->driver.client_generation;
    if (token.operation_id == 0u)
        token.operation_id = fixture->next_operation_id++;
    if (token.correlation_id == 0u)
        token.correlation_id = fixture->next_correlation_id++;
    return token;
}

bool fram_driver_fixture_take_completion(
    FramDriverFixture *fixture, StorageIoCompletion *completion_out)
{
    if (!fixture || !completion_out || !fixture->completion_ready)
        return false;
    *completion_out = fixture->completion;
    fixture->completion_ready = false;
    return true;
}

StorageIoSubmitResult fram_driver_fixture_probe(
    FramDriverFixture *fixture, uint64_t deadline_us)
{
    return fixture
        ? fram_probe_async(&fixture->driver,
                           fram_driver_fixture_token(fixture), deadline_us)
        : STORAGE_IO_SUBMIT_INVALID_PARAM;
}

StorageIoSubmitResult fram_driver_fixture_read(
    FramDriverFixture *fixture, uint16_t address, uint8_t *buffer,
    uint16_t length, uint64_t deadline_us)
{
    return fixture
        ? fram_read_async(&fixture->driver, address, buffer, length,
                          fram_driver_fixture_token(fixture), deadline_us)
        : STORAGE_IO_SUBMIT_INVALID_PARAM;
}

StorageIoSubmitResult fram_driver_fixture_write(
    FramDriverFixture *fixture, uint16_t address, const uint8_t *buffer,
    uint16_t length, uint64_t deadline_us)
{
    return fixture
        ? fram_write_async(&fixture->driver, address, buffer, length,
                           fram_driver_fixture_token(fixture), deadline_us)
        : STORAGE_IO_SUBMIT_INVALID_PARAM;
}
