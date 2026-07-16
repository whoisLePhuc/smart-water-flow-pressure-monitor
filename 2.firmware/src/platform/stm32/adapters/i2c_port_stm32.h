#ifndef SWFPM_I2C_PORT_STM32_H
#define SWFPM_I2C_PORT_STM32_H

#include <stdbool.h>
#include "ports/i2c_port.h"

typedef enum {
    STM32_ASYNC_HAL_OK,
    STM32_ASYNC_HAL_BUSY,
    STM32_ASYNC_HAL_ERROR
} Stm32AsyncHalStatus;

typedef struct {
    Stm32AsyncHalStatus (*start)(void *hal_i2c,
                                 const I2cPortRequest *request);
    Stm32AsyncHalStatus (*cancel)(void *hal_i2c);
    Stm32AsyncHalStatus (*recover)(void *hal_i2c);
} Stm32I2cHalOps;

typedef void (*Stm32I2cCompletionSink)(
    void *context, const I2cPortRequest *request, PortStatus result);

typedef struct {
    void *hal_i2c;
    const Stm32I2cHalOps *ops;
    Stm32I2cCompletionSink completion_sink;
    void *completion_context;
    I2cPortRequest active_request;
    bool active;
} Stm32I2cAdapter;

PortStatus i2c_port_stm32_init(Stm32I2cAdapter *adapter,
                               void *hal_i2c,
                               const Stm32I2cHalOps *ops,
                               Stm32I2cCompletionSink sink,
                               void *sink_context,
                               I2cPort *port_out);

// Board HAL callbacks call this after leaving the peripheral IRQ handler or
// from a deferred IRQ callback. It never directly mutates an event queue.
void i2c_port_stm32_on_complete(Stm32I2cAdapter *adapter,
                                PortStatus result);

#endif /* SWFPM_I2C_PORT_STM32_H */
