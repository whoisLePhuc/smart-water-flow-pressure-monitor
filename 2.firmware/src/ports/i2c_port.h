#ifndef SWFPM_I2C_PORT_H
#define SWFPM_I2C_PORT_H

#include <stdbool.h>
#include <stdint.h>
#include "ports/port_status.h"

typedef struct {
    uint32_t transaction_id;
    uint32_t correlation_id;
    uint32_t client_generation;
    uint32_t bus_generation;
    uint8_t slave_address;
    const uint8_t *tx;
    uint16_t tx_length;
    uint8_t *rx;
    uint16_t rx_length;
    uint64_t deadline_us;
} I2cPortRequest;

typedef struct {
    void *context;
    PortStatus (*submit)(void *context, const I2cPortRequest *request);
    PortStatus (*cancel)(void *context, uint32_t transaction_id,
                         uint32_t bus_generation);
    PortStatus (*recover)(void *context, uint32_t new_bus_generation);
} I2cPort;

static inline bool i2c_port_is_valid(const I2cPort *port)
{
    return port && port->submit;
}

#endif
