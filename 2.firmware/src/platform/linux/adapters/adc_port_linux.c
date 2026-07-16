#include "adc_port_linux.h"

static PortStatus linux_adc_read(void *context,
                                 AdcChannel channel,
                                 uint16_t *raw_value)
{
    LinuxAdcAdapter *adapter = (LinuxAdcAdapter *)context;
    if (!adapter || !raw_value || channel >= ADC_CHANNEL_COUNT)
        return PORT_STATUS_INVALID_PARAM;
    if (adapter->fault != PORT_OK)
        return adapter->fault;
    *raw_value = adapter->value;
    return PORT_OK;
}

void adc_port_linux_init(LinuxAdcAdapter *adapter, AdcPort *port_out)
{
    if (!adapter || !port_out)
        return;
    adapter->value = 2048u;
    adapter->fault = PORT_OK;
    port_out->context = adapter;
    port_out->read = linux_adc_read;
}

void adc_port_linux_set_value(LinuxAdcAdapter *adapter, uint16_t value)
{
    if (adapter)
        adapter->value = value;
}

void adc_port_linux_set_fault(LinuxAdcAdapter *adapter, PortStatus fault)
{
    if (adapter)
        adapter->fault = fault;
}
