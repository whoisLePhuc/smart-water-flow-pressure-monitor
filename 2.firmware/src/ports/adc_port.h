#ifndef SWFPM_ADC_PORT_H
#define SWFPM_ADC_PORT_H

#include <stdint.h>
#include "port_status.h"

typedef enum {
    ADC_CHANNEL_BATTERY = 0,
    ADC_CHANNEL_COUNT
} AdcChannel;

PortStatus adc_port_read(AdcChannel channel, uint16_t *raw_value);

#endif
