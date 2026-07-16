#include "storage_port.h"
#include <string.h>

static uint8_t s_storage[512];

PortStatus storage_port_read(uint32_t offset, uint8_t *buffer, uint16_t size)
{
    if (!buffer) return PORT_STATUS_INVALID_PARAM;
    if (offset + size > sizeof(s_storage)) return PORT_STATUS_INVALID_PARAM;
    memcpy(buffer, s_storage + offset, size);
    return PORT_OK;
}

PortStatus storage_port_write(uint32_t offset, const uint8_t *data, uint16_t size)
{
    if (!data) return PORT_STATUS_INVALID_PARAM;
    if (offset + size > sizeof(s_storage)) return PORT_STATUS_INVALID_PARAM;
    memcpy(s_storage + offset, data, size);
    return PORT_OK;
}
