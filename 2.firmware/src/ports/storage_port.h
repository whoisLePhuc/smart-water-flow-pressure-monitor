#ifndef SWFPM_STORAGE_PORT_H
#define SWFPM_STORAGE_PORT_H

#include <stdint.h>
#include "port_status.h"

PortStatus storage_port_read(uint32_t offset, uint8_t *buffer, uint16_t size);
PortStatus storage_port_write(uint32_t offset, const uint8_t *data, uint16_t size);

#endif
