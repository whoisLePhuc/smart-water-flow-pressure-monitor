#ifndef SWFPM_FRAM_DRIVER_H
#define SWFPM_FRAM_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "domain/common/metadata.h"
#include "domain/common/status.h"

/* FM24CL04B constants */
#define FM24CL04B_SIZE_BYTES  512u
#define FM24CL04B_PAGE_MASK   0x100u  /* bit 8 selects I2C slave page */

typedef enum {
    FRAM_DRV_OK,
    FRAM_DRV_OUT_OF_RANGE,
    FRAM_DRV_BUSY,
    FRAM_DRV_IO_ERROR,
    FRAM_DRV_INVALID_PARAM
} FramDriverStatus;

typedef struct {
    uint32_t operation_id;
    uint32_t transaction_id;
    FramDriverStatus status;
    uint16_t transferred;
} FramDriverCompletion;

// The in-memory backend is used when use_i2c is false. The current driver API
// is synchronous; i2c_bus_context is reserved for integration and is not a
// StoragePort abstraction.
typedef struct {
    uint8_t memory[FM24CL04B_SIZE_BYTES]; /* Driver-owned test backing store. */
    bool use_i2c;                          /* Selects the external bus path. */
    bool     write_protected;
    uint32_t operation_counter;            /* Monotonic diagnostic identity. */
    void *i2c_bus_context;                 /* Borrowed I2cBusManager; nullable. */
    uint8_t slave_addr_base;
} FramDriver;

// i2c_bus is borrowed and must outlive self when use_i2c is true. A NULL bus
// selects the driver-owned in-memory backend.
void FramDriver_Init(FramDriver *self, bool use_i2c, void *i2c_bus, uint8_t slave_addr);

// buffer remains caller-owned. It is written only when the requested logical
// range is valid and the operation succeeds.
FramDriverStatus FramDriver_Read(FramDriver *self, uint16_t address,
                                 uint8_t *buffer, uint16_t length);

// data is borrowed for the duration of this synchronous call. Write protection
// and range failures leave the backing store unchanged.
FramDriverStatus FramDriver_Write(FramDriver *self, uint16_t address,
                                  const uint8_t *buffer, uint16_t length);

void FramDriver_SetWriteProtect(FramDriver *self, bool protected);

#endif /* SWFPM_FRAM_DRIVER_H */
