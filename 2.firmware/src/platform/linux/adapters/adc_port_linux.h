#ifndef SWFPM_ADC_PORT_LINUX_H
#define SWFPM_ADC_PORT_LINUX_H

#include <stdint.h>
#include "port_status.h"

void adc_port_linux_set_value(uint16_t value);
void adc_port_linux_set_fault(PortStatus fault);

#endif
