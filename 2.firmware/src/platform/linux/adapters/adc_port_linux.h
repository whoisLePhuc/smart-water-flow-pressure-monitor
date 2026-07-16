#ifndef SWFPM_ADC_PORT_LINUX_H
#define SWFPM_ADC_PORT_LINUX_H

#include <stdint.h>
#include "adc_port.h"

typedef struct {
    uint16_t   value;
    PortStatus fault;
} LinuxAdcAdapter;

void adc_port_linux_init(LinuxAdcAdapter *adapter, AdcPort *port_out);
void adc_port_linux_set_value(LinuxAdcAdapter *adapter, uint16_t value);
void adc_port_linux_set_fault(LinuxAdcAdapter *adapter, PortStatus fault);

#endif
