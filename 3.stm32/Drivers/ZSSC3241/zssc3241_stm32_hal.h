/**
  ******************************************************************************
  * @file    zssc3241_stm32_hal.h
  * @brief   STM32 HAL transport adapter for the ZSSC3241 core driver
  ******************************************************************************
  */

#ifndef SWFPM_ZSSC3241_STM32_HAL_H
#define SWFPM_ZSSC3241_STM32_HAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "zssc3241.h"

typedef struct {
    I2C_HandleTypeDef *hi2c;
    GPIO_TypeDef *reset_port;
    uint16_t reset_pin;
} Zssc3241Stm32HalContext;

/**
 * Configure a transport that uses blocking STM32 HAL I2C calls.
 *
 * reset_port may be NULL when the RESQ pin is not controlled by the MCU.
 * The adapter shifts the driver's 7-bit address left by one before passing it
 * to STM32 HAL.
 */
Zssc3241Status ZSSC3241_Stm32HalInitTransport(
    Zssc3241Stm32HalContext *context,
    I2C_HandleTypeDef *hi2c,
    GPIO_TypeDef *reset_port,
    uint16_t reset_pin,
    Zssc3241Transport *transport);

#ifdef __cplusplus
}
#endif

#endif /* SWFPM_ZSSC3241_STM32_HAL_H */
