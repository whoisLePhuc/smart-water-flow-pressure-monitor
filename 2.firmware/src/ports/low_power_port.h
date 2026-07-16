#ifndef SWFPM_LOW_POWER_PORT_H
#define SWFPM_LOW_POWER_PORT_H

#include "ports/port_status.h"

typedef struct {
    void *context;
    PortStatus (*prepare)(void *context);
    PortStatus (*enter_stop2)(void *context);
    PortStatus (*resume_clocks)(void *context);
} LowPowerPort;

#endif /* SWFPM_LOW_POWER_PORT_H */
