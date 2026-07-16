#include "spi_port_stm32.h"
#include <string.h>

static PortStatus submit(void *context, const SpiPortRequest *request)
{
    Stm32SpiAdapter *adapter = context;
    if (!adapter || !request || adapter->active)
        return adapter && adapter->active ? PORT_STATUS_BUSY
                                          : PORT_STATUS_INVALID_PARAM;
    PortStatus status = adapter->ops->start(adapter->hal_spi, request);
    if (status != PORT_OK) return status;
    adapter->active_request = *request;
    adapter->active = true;
    return PORT_OK;
}

static PortStatus cancel(void *context, uint32_t transaction_id,
                         uint32_t bus_generation)
{
    Stm32SpiAdapter *adapter = context;
    if (!adapter || !adapter->active ||
        adapter->active_request.transaction_id != transaction_id ||
        adapter->active_request.bus_generation != bus_generation)
        return PORT_STATUS_INVALID_PARAM;
    PortStatus status = adapter->ops->cancel(adapter->hal_spi);
    if (status == PORT_OK) adapter->active = false;
    return status;
}

static PortStatus recover(void *context, uint32_t generation)
{
    Stm32SpiAdapter *adapter = context;
    (void)generation;
    if (!adapter) return PORT_STATUS_INVALID_PARAM;
    PortStatus status = adapter->ops->recover(adapter->hal_spi);
    if (status == PORT_OK) adapter->active = false;
    return status;
}

PortStatus spi_port_stm32_init(Stm32SpiAdapter *adapter,
                               void *hal_spi,
                               const Stm32SpiHalOps *ops,
                               Stm32SpiCompletionSink sink,
                               void *sink_context,
                               SpiPort *port_out)
{
    if (!adapter || !hal_spi || !ops || !ops->start || !ops->cancel ||
        !ops->recover || !sink || !port_out)
        return PORT_STATUS_INVALID_PARAM;
    memset(adapter, 0, sizeof(*adapter));
    adapter->hal_spi = hal_spi;
    adapter->ops = ops;
    adapter->completion_sink = sink;
    adapter->completion_context = sink_context;
    port_out->context = adapter;
    port_out->submit = submit;
    port_out->cancel = cancel;
    port_out->recover = recover;
    return PORT_OK;
}

void spi_port_stm32_on_complete(Stm32SpiAdapter *adapter,
                                PortStatus result)
{
    if (!adapter || !adapter->active) return;
    SpiPortRequest completed = adapter->active_request;
    adapter->active = false;
    adapter->completion_sink(adapter->completion_context, &completed, result);
}
