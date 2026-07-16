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
    void *context;       /* Borrowed adapter state; must outlive this port. */
    AdcPortReadFn read;  /* Required; may perform a bounded synchronous read. */
} AdcPort;

// raw_value is written only on PORT_OK. Implementations must document whether
// read is synchronous and bound their worst-case execution time.
static inline PortStatus adc_port_read(const AdcPort *port,
                                       AdcChannel channel,
                                       uint16_t *raw_value)
{
    if (!port || !port->read || !raw_value)
        return PORT_STATUS_INVALID_PARAM;
    return port->read(port->context, channel, raw_value);
}

#endif
