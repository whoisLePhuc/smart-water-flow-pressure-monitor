#ifndef SWFPM_SPI_PORT_H
#define SWFPM_SPI_PORT_H

#include <stdbool.h>
#include <stdint.h>
#include "ports/port_status.h"

typedef struct {
    uint32_t transaction_id;
    uint32_t correlation_id;
    uint32_t client_generation;
    uint32_t bus_generation;
    uint8_t chip_select;
    const uint8_t *tx;
    uint8_t *rx;
    uint16_t length;
    uint64_t deadline_us;
} SpiPortRequest;

typedef struct {
    void *context;
    PortStatus (*submit)(void *context, const SpiPortRequest *request);
    PortStatus (*cancel)(void *context, uint32_t transaction_id,
                         uint32_t bus_generation);
    PortStatus (*recover)(void *context, uint32_t new_bus_generation);
} SpiPort;

static inline bool spi_port_is_valid(const SpiPort *port)
{
    return port && port->submit;
}

#endif /* SWFPM_SPI_PORT_H */
