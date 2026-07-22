#ifndef SWFPM_STM32_TEST_PLATFORM_H
#define SWFPM_STM32_TEST_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/stm32/adapters/i2c_port_stm32.h"
#include "platform/stm32/stm32_i2c1_hal.h"

typedef void (*Stm32TestI2cSink)(void *context,
                                 const I2cPortRequest *request,
                                 PortStatus result);

typedef struct {
    I2C_HandleTypeDef *handle;
    Stm32I2c1Hal hal;
    Stm32I2cAdapter adapter;
    I2cPort port;
    Stm32TestI2cSink sink;
    void *sink_context;
    uint32_t unmatched_completion_count;
    bool initialized;
} Stm32TestPlatform;

bool stm32_test_platform_init(Stm32TestPlatform *platform,
                              I2C_HandleTypeDef *handle);
void stm32_test_platform_set_sink(Stm32TestPlatform *platform,
                                  Stm32TestI2cSink sink,
                                  void *sink_context);
void stm32_test_platform_poll(Stm32TestPlatform *platform);
bool stm32_test_platform_recover(Stm32TestPlatform *platform);

#endif /* SWFPM_STM32_TEST_PLATFORM_H */
