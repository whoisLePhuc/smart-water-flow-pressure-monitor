#include "drivers/storage/fram_driver.h"
#include <string.h>

void FramDriver_Init(FramDriver *self, bool use_i2c, void *i2c_bus, uint8_t slave_addr)
{
    memset(self, 0, sizeof(*self));
    self->use_i2c = use_i2c;
    self->i2c_bus_context = i2c_bus;
    self->slave_addr_base = slave_addr;
    self->write_protected = false;
    self->operation_counter = 0;
}

FramDriverStatus FramDriver_Read(FramDriver *self, uint16_t address,
                                 uint8_t *buffer, uint16_t length)
{
    if (!self || !buffer)
        return FRAM_DRV_INVALID_PARAM;

    /* Bounds check: address must be < 512, address+length must be <= 512 */
    if (address >= FM24CL04B_SIZE_BYTES)
        return FRAM_DRV_OUT_OF_RANGE;
    if (length == 0)
        return FRAM_DRV_OK;
    if ((uint32_t)address + (uint32_t)length > FM24CL04B_SIZE_BYTES)
        return FRAM_DRV_OUT_OF_RANGE;

    if (self->use_i2c) {
        /* I2C mode: delegate to bus manager (stub for future implementation) */
        (void)self->i2c_bus_context;
        return FRAM_DRV_IO_ERROR;  /* not yet implemented */
    }

    /* Buffer mode: copy from internal memory */
    memcpy(buffer, self->memory + address, length);
    self->operation_counter++;
    return FRAM_DRV_OK;
}

FramDriverStatus FramDriver_Write(FramDriver *self, uint16_t address,
                                  const uint8_t *buffer, uint16_t length)
{
    if (!self || !buffer)
        return FRAM_DRV_INVALID_PARAM;

    if (self->write_protected)
        return FRAM_DRV_IO_ERROR;

    if (address >= FM24CL04B_SIZE_BYTES)
        return FRAM_DRV_OUT_OF_RANGE;
    if (length == 0)
        return FRAM_DRV_OK;
    if ((uint32_t)address + (uint32_t)length > FM24CL04B_SIZE_BYTES)
        return FRAM_DRV_OUT_OF_RANGE;

    if (self->use_i2c) {
        (void)self->i2c_bus_context;
        return FRAM_DRV_IO_ERROR;
    }

    memcpy(self->memory + address, buffer, length);
    self->operation_counter++;
    return FRAM_DRV_OK;
}

void FramDriver_SetWriteProtect(FramDriver *self, bool protected)
{
    if (self)
        self->write_protected = protected;
}
