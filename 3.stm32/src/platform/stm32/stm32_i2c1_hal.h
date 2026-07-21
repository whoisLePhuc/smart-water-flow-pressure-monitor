#ifndef SWFPM_STM32_I2C1_HAL_H
#define SWFPM_STM32_I2C1_HAL_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/stm32/adapters/i2c_port_stm32.h"
#include "stm32l4xx_hal.h"

/** @brief Phases of a multi-step I2C1 transfer sequence. */
typedef enum {
    STM32_I2C1_PHASE_IDLE = 0,          /**< No transfer in progress. */
    STM32_I2C1_PHASE_TX_ONLY,           /**< Transmit-only transfer. */
    STM32_I2C1_PHASE_RX_ONLY,           /**< Receive-only transfer. */
    STM32_I2C1_PHASE_TX_THEN_RX_TX,     /**< Combined: first (transmit) phase. */
    STM32_I2C1_PHASE_TX_THEN_RX_RX      /**< Combined: second (receive) phase. */
} Stm32I2c1Phase;

/** @brief Runtime state for I2C1 interrupt-driven transfers. */
typedef struct {
    I2C_HandleTypeDef* handle;          /**< Borrowed CubeMX I2C1 handle. */
    Stm32I2cAdapter* adapter;           /**< Borrowed portable STM32 adapter. */

    uint16_t device_address;            /**< HAL address: 7-bit address << 1. */
    uint8_t* rx_buffer;                 /**< Buffer for the second transfer phase. */
    uint16_t rx_length;                 /**< Byte count for the second transfer. */

    volatile Stm32I2c1Phase phase;          /**< Current phase of the transfer sequence. */
    volatile bool transfer_active;          /**< True while HAL transfer is in progress. */
    volatile bool completion_pending;       /**< True when completion is ready for delivery. */
    volatile PortStatus completion_result;  /**< Cached result of the completed transfer. */
    volatile uint32_t last_hal_error;       /**< Last HAL error code from I2C_GetError. */
} Stm32I2c1Hal;

/**
 * @brief Initialise the I2C1 HAL adapter.
 * @note  Must be called after MX_I2C1_Init().
 * @param context  Uninitialised instance to set up.
 * @param handle   Initialised CubeMX I2C1 handle.
 * @param adapter  Portable STM32 I2C adapter for completion delivery.
 * @return true on success, false on invalid arguments or duplicate init.
 */
bool stm32_i2c1_hal_init(Stm32I2c1Hal* context,
                         I2C_HandleTypeDef* handle,
                         Stm32I2cAdapter* adapter);

/**
 * @brief Get the I2C1 HAL operation table.
 * @return Pointer to the static Stm32I2cHalOps vtable.
 */
const Stm32I2cHalOps* stm32_i2c1_hal_ops(void);

/**
 * @brief Deliver completion events from IRQ context to the portable adapter.
 * @note  Call continuously from the cooperative main loop.
 * @param context  Initialised I2C1 HAL instance.
 */
void stm32_i2c1_hal_poll(Stm32I2c1Hal* context);

/**
 * @brief Get the last HAL error code.
 * @param context  Initialised I2C1 HAL instance.
 * @return HAL error code, or HAL_I2C_ERROR_INVALID_PARAM if context is NULL.
 */
uint32_t stm32_i2c1_hal_last_error(const Stm32I2c1Hal* context);

#endif /* SWFPM_STM32_I2C1_HAL_H */