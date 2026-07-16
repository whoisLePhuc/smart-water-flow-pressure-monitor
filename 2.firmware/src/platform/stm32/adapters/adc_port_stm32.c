#include "adc_port_stm32.h"
#include <string.h>

static PortStatus map_status(Stm32AdcHalStatus status)
{
    switch (status) {
    case STM32_ADC_HAL_OK: return PORT_OK;
    case STM32_ADC_HAL_BUSY: return PORT_STATUS_BUSY;
    case STM32_ADC_HAL_TIMEOUT: return PORT_STATUS_TIMEOUT;
    default: return PORT_STATUS_HARDWARE_ERROR;
    }
}

static PortStatus stm32_adc_read(void *context,
                                 AdcChannel channel,
                                 uint16_t *raw_value)
{
    Stm32AdcAdapter *adapter = (Stm32AdcAdapter *)context;
    if (!adapter || !adapter->initialized || !raw_value
        || channel >= ADC_CHANNEL_COUNT)
        return PORT_STATUS_INVALID_PARAM;

    Stm32AdcHalStatus status = adapter->ops->configure_channel(
        adapter->hal_adc, adapter->channel_map[channel]);
    if (status != STM32_ADC_HAL_OK) return map_status(status);

    status = adapter->ops->start(adapter->hal_adc);
    if (status != STM32_ADC_HAL_OK) return map_status(status);

    status = adapter->ops->poll(adapter->hal_adc, adapter->timeout_ms);
    if (status != STM32_ADC_HAL_OK) {
        (void)adapter->ops->stop(adapter->hal_adc);
        return map_status(status);
    }

    uint32_t value = 0;
    status = adapter->ops->read_value(adapter->hal_adc, &value);
    Stm32AdcHalStatus stop_status = adapter->ops->stop(adapter->hal_adc);
    if (status != STM32_ADC_HAL_OK) return map_status(status);
    if (stop_status != STM32_ADC_HAL_OK) return map_status(stop_status);
    if (value > UINT16_MAX) return PORT_STATUS_HARDWARE_ERROR;

    *raw_value = (uint16_t)value;
    return PORT_OK;
}

PortStatus adc_port_stm32_init(Stm32AdcAdapter *adapter,
                               void *hal_adc,
                               const Stm32AdcHalOps *ops,
                               uint32_t battery_channel,
                               uint32_t timeout_ms,
                               AdcPort *port_out)
{
    if (!adapter || !hal_adc || !ops || !port_out
        || !ops->configure_channel || !ops->start || !ops->poll
        || !ops->read_value || !ops->stop)
        return PORT_STATUS_INVALID_PARAM;

    memset(adapter, 0, sizeof(*adapter));
    adapter->hal_adc = hal_adc;
    adapter->ops = ops;
    adapter->channel_map[ADC_CHANNEL_BATTERY] = battery_channel;
    adapter->timeout_ms = timeout_ms;
    adapter->initialized = true;
    port_out->context = adapter;
    port_out->read = stm32_adc_read;
    return PORT_OK;
}
