#ifndef SWFPM_STM32_I2C1_HAL_H
#define SWFPM_STM32_I2C1_HAL_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32l4xx_hal.h"
#include "platform/stm32/adapters/i2c_port_stm32.h"

typedef enum {
    STM32_I2C1_PHASE_IDLE = 0,
    STM32_I2C1_PHASE_TX_ONLY,
    STM32_I2C1_PHASE_RX_ONLY,
    STM32_I2C1_PHASE_TX_THEN_RX_TX,
    STM32_I2C1_PHASE_TX_THEN_RX_RX
} Stm32I2c1Phase;

typedef struct {
    I2C_HandleTypeDef *handle;     /* Borrowed CubeMX I2C1 handle. */
    Stm32I2cAdapter *adapter;      /* Borrowed portable STM32 adapter. */

    uint16_t device_address;       /* HAL address: 7-bit address << 1. */
    uint8_t *rx_buffer;            /* Used by the second transfer phase. */
    uint16_t rx_length;

    volatile Stm32I2c1Phase phase;
    volatile bool transfer_active;
    volatile bool completion_pending;
    volatile PortStatus completion_result;
    volatile uint32_t last_hal_error;
} Stm32I2c1Hal;

/**
 * Must be called after MX_I2C1_Init().
 */
bool stm32_i2c1_hal_init(Stm32I2c1Hal *context,
                         I2C_HandleTypeDef *handle,
                         Stm32I2cAdapter *adapter);

const Stm32I2cHalOps *stm32_i2c1_hal_ops(void);

/**
 * Delivers completion from IRQ context to the portable adapter.
 * Call continuously from the cooperative main loop.
 */
void stm32_i2c1_hal_poll(Stm32I2c1Hal *context);

uint32_t stm32_i2c1_hal_last_error(
    const Stm32I2c1Hal *context);

#endif /* SWFPM_STM32_I2C1_HAL_H */