#include "support/storage_fixture.h"

#include <string.h>

static void route_app_completion(void *context,
                                 const I2cPortRequest *request,
                                 PortStatus result)
{
    StorageFixture *fixture = context;
    if (!fixture || !app_composition_on_i2c_port_completion(
                        &fixture->app, request, result)) {
        if (fixture)
            fixture->unmatched_completion_count++;
    }
}

static bool build_graph(StorageFixture *fixture)
{
    stm32_test_platform_set_sink(fixture->platform,
                                 route_app_completion, fixture);
    memset(&fixture->app, 0, sizeof(fixture->app));
    fixture->initialized = app_composition_init(
        &fixture->app, &fixture->dependencies);
    return fixture->initialized;
}

bool storage_fixture_init(StorageFixture *fixture,
                          Stm32TestPlatform *platform)
{
    if (!fixture || !platform || !platform->initialized)
        return false;
    (void)stm32_test_platform_recover(platform);
    memset(fixture, 0, sizeof(*fixture));
    fixture->platform = platform;
    fixture->dependencies = (AppCompositionDependencies){
        .shared_i2c_port = &platform->port,
        .fram_config = {
            .client_id = 1u,
            .slave_address_base_7bit = 0x50u,
            .capacity_bytes = FM24CL04B_SIZE_BYTES,
            .max_chunk_bytes = FM24CL04B_MAX_CHUNK_BYTES,
            .bus_priority = 3u
        },
        .storage_io_timeout_us = 250000u
    };
    return build_graph(fixture);
}

bool storage_fixture_reboot(StorageFixture *fixture)
{
    if (!fixture || !fixture->platform)
        return false;
    (void)stm32_test_platform_recover(fixture->platform);
    return build_graph(fixture);
}

void storage_fixture_poll(StorageFixture *fixture, uint64_t now_us)
{
    if (!fixture || !fixture->initialized)
        return;
    stm32_test_platform_poll(fixture->platform);
    app_composition_poll(&fixture->app, now_us);
}
