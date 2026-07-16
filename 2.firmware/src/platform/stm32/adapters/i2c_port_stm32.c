#include "i2c_port_stm32.h"
#include <string.h>

static PortStatus map_hal(Stm32AsyncHalStatus status)
{
    if (status == STM32_ASYNC_HAL_OK) return PORT_OK;
    if (status == STM32_ASYNC_HAL_BUSY) return PORT_STATUS_BUSY;
    return PORT_STATUS_HARDWARE_ERROR;
}

static PortStatus submit(void *context, const I2cPortRequest *request)
{
    Stm32I2cAdapter *adapter = context;
    if (!adapter || !request || adapter->active)
        return adapter && adapter->active ? PORT_STATUS_BUSY
                                          : PORT_STATUS_INVALID_PARAM;
    Stm32AsyncHalStatus status = adapter->ops->start(adapter->hal_i2c,
                                                      request);
    if (status != STM32_ASYNC_HAL_OK) return map_hal(status);
    adapter->active_request = *request;
    adapter->active = true;
    return PORT_OK;
}

static PortStatus cancel(void *context, uint32_t transaction_id,
                         uint32_t bus_generation)
{
    Stm32I2cAdapter *adapter = context;
    if (!adapter || !adapter->active ||
        adapter->active_request.transaction_id != transaction_id ||
        adapter->active_request.bus_generation != bus_generation)
        return PORT_STATUS_INVALID_PARAM;
    PortStatus status = map_hal(adapter->ops->cancel(adapter->hal_i2c));
    if (status == PORT_OK) adapter->active = false;
    return status;
}

static PortStatus recover(void *context, uint32_t new_bus_generation)
{
    Stm32I2cAdapter *adapter = context;
    (void)new_bus_generation;
    if (!adapter) return PORT_STATUS_INVALID_PARAM;
    PortStatus status = map_hal(adapter->ops->recover(adapter->hal_i2c));
    if (status == PORT_OK) adapter->active = false;
    return status;
}

PortStatus i2c_port_stm32_init(Stm32I2cAdapter *adapter,
                               void *hal_i2c,
                               const Stm32I2cHalOps *ops,
                               Stm32I2cCompletionSink sink,
                               void *sink_context,
                               I2cPort *port_out)
{
    if (!adapter || !hal_i2c || !ops || !ops->start || !ops->cancel ||
        !ops->recover || !sink || !port_out)
        return PORT_STATUS_INVALID_PARAM;
    memset(adapter, 0, sizeof(*adapter));
    adapter->hal_i2c = hal_i2c;
    adapter->ops = ops;
    adapter->completion_sink = sink;
    adapter->completion_context = sink_context;
    port_out->context = adapter;
    port_out->submit = submit;
    port_out->cancel = cancel;
    port_out->recover = recover;
    return PORT_OK;
}

void i2c_port_stm32_on_complete(Stm32I2cAdapter *adapter,
                                PortStatus result)
{
    if (!adapter || !adapter->active) return;
    I2cPortRequest completed = adapter->active_request;
    adapter->active = false;
    adapter->completion_sink(adapter->completion_context, &completed, result);
}
