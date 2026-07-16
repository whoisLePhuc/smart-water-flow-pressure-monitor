#ifndef SWFPM_ADC_PORT_H
#define SWFPM_ADC_PORT_H

#include <stdint.h>
#include "port_status.h"

typedef enum {
    ADC_CHANNEL_BATTERY = 0,
    ADC_CHANNEL_COUNT
} AdcChannel;

typedef PortStatus (*AdcPortReadFn)(void *context,
                                    AdcChannel channel,
                                    uint16_t *raw_value);

typedef struct {
    void          *context;
    AdcPortReadFn  read;
} AdcPort;

static inline PortStatus adc_port_read(const AdcPort *port,
                                       AdcChannel channel,
                                       uint16_t *raw_value)
{
    if (!port || !port->read || !raw_value)
        return PORT_STATUS_INVALID_PARAM;
    return port->read(port->context, channel, raw_value);
}

#endif
