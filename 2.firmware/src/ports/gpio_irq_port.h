#ifndef SWFPM_GPIO_IRQ_PORT_H
#define SWFPM_GPIO_IRQ_PORT_H

#include <stdbool.h>
#include <stdint.h>
#include "ports/port_status.h"

typedef struct {
    void *context;
    PortStatus (*arm)(void *context, uint32_t line, bool rising_edge,
                      bool falling_edge);
    PortStatus (*disarm)(void *context, uint32_t line);
    PortStatus (*clear_pending)(void *context, uint32_t line);
} GpioIrqPort;

#endif /* SWFPM_GPIO_IRQ_PORT_H */
