#ifndef SWFPM_FRAM_DRIVER_H
#define SWFPM_FRAM_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "domain/common/metadata.h"
#include "domain/common/status.h"

/* FM24CL04B constants */
#define FM24CL04B_SIZE_BYTES  512u
#define FM24CL04B_PAGE_MASK   0x100u  /* bit 8 selects I2C slave page */

/* Driver status */
typedef enum {
    FRAM_DRV_OK,
    FRAM_DRV_OUT_OF_RANGE,
    FRAM_DRV_BUSY,
    FRAM_DRV_IO_ERROR,
    FRAM_DRV_INVALID_PARAM
} FramDriverStatus;

/* Operation completion payload */
typedef struct {
    uint32_t operation_id;
    uint32_t transaction_id;
    FramDriverStatus status;
    uint16_t transferred;
} FramDriverCompletion;

/* Driver instance — operates on a simple buffer for test,
 * or delegates to I2C bus manager for real hardware. */
typedef struct {
    uint8_t  memory[FM24CL04B_SIZE_BYTES];  /* in-memory backing store */
    bool     use_i2c;                        /* true=delegate to I2C bus */
    bool     write_protected;
    uint32_t operation_counter;
    void    *i2c_bus_context;               /* I2cBusManager* when use_i2c=true */
    uint8_t  slave_addr_base;               /* base 7-bit address (e.g. 0x50) */
} FramDriver;

/* Initialize driver. If i2c_bus is NULL or use_i2c=false, uses internal buffer. */
void FramDriver_Init(FramDriver *self, bool use_i2c, void *i2c_bus, uint8_t slave_addr);

/* Read bytes from F-RAM at logical address (0..511).
 * Returns OK if address+length within bounds. */
FramDriverStatus FramDriver_Read(FramDriver *self, uint16_t address,
                                 uint8_t *buffer, uint16_t length);

/* Write bytes to F-RAM at logical address (0..511).
 * Returns OK if address+length within bounds and not write-protected. */
FramDriverStatus FramDriver_Write(FramDriver *self, uint16_t address,
                                  const uint8_t *buffer, uint16_t length);

/* Set/get write protection */
void FramDriver_SetWriteProtect(FramDriver *self, bool protected);

#endif /* SWFPM_FRAM_DRIVER_H */
