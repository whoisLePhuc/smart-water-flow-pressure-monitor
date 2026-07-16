#ifndef SWFPM_WATCHDOG_PORT_H
#define SWFPM_WATCHDOG_PORT_H

#include <stdint.h>
#include "ports/port_status.h"

typedef struct {
    void *context;
    PortStatus (*start)(void *context, uint32_t timeout_ms);
    PortStatus (*refresh)(void *context);
} WatchdogPort;

#endif /* SWFPM_WATCHDOG_PORT_H */
