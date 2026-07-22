#include "support/stm32_test_platform.h"

#include <string.h>

static void route_completion(void *context,
                             const I2cPortRequest *request,
                             PortStatus result)
{
    Stm32TestPlatform *platform = context;
    if (!platform)
        return;
    if (platform->sink)
        platform->sink(platform->sink_context, request, result);
    else
        platform->unmatched_completion_count++;
}

bool stm32_test_platform_init(Stm32TestPlatform *platform,
                              I2C_HandleTypeDef *handle)
{
    if (!platform || !handle)
        return false;
    memset(platform, 0, sizeof(*platform));
    platform->handle = handle;
    if (!stm32_i2c1_hal_init(&platform->hal, handle, &platform->adapter))
        return false;
    if (i2c_port_stm32_init(&platform->adapter, &platform->hal,
                            stm32_i2c1_hal_ops(), route_completion,
                            platform, &platform->port) != PORT_OK)
        return false;
    platform->initialized = true;
    return true;
}

void stm32_test_platform_set_sink(Stm32TestPlatform *platform,
                                  Stm32TestI2cSink sink,
                                  void *sink_context)
{
    if (!platform)
        return;
    platform->sink = sink;
    platform->sink_context = sink_context;
}

void stm32_test_platform_poll(Stm32TestPlatform *platform)
{
    if (platform && platform->initialized)
        stm32_i2c1_hal_poll(&platform->hal);
}

bool stm32_test_platform_recover(Stm32TestPlatform *platform)
{
    if (!platform || !platform->initialized || !platform->port.recover)
        return false;
    stm32_test_platform_set_sink(platform, NULL, NULL);
    return platform->port.recover(platform->port.context,
                                  1u) == PORT_OK;
}
