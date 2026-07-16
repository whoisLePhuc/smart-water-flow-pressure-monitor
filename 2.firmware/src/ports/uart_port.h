#ifndef SWFPM_UART_PORT_H
#define SWFPM_UART_PORT_H

#include <stdint.h>
#include "ports/port_status.h"

typedef struct {
    void *context;
    PortStatus (*write)(void *context, const uint8_t *data, uint16_t length,
                        uint32_t correlation_id);
    PortStatus (*read)(void *context, uint8_t *data, uint16_t capacity,
                       uint32_t correlation_id);
} UartPort;

#endif /* SWFPM_UART_PORT_H */
