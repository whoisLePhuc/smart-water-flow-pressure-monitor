#include "adc_port.h"
#include "adc_port_linux.h"

static uint16_t s_fake_value = 2048;
static PortStatus s_fault = PORT_OK;

void adc_port_linux_set_value(uint16_t value) { s_fake_value = value; }
void adc_port_linux_set_fault(PortStatus fault) { s_fault = fault; }

PortStatus adc_port_read(AdcChannel channel, uint16_t *raw_value)
{
    if (!raw_value) return PORT_STATUS_INVALID_PARAM;
    if (channel >= ADC_CHANNEL_COUNT) return PORT_STATUS_INVALID_PARAM;
    if (s_fault != PORT_OK) return s_fault;
    *raw_value = s_fake_value;
    return PORT_OK;
}
