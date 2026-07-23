/**
  ******************************************************************************
  * @file    max35103_stm32_hal.h
  * @brief   STM32 HAL transport adapter for the portable MAX35103 driver
  ******************************************************************************
  */

#ifndef SWFPM_MAX35103_STM32_HAL_H
#define SWFPM_MAX35103_STM32_HAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "max35103.h"

/** STM32 resources owned by the board composition layer. */
typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *nss_port;
    uint16_t nss_pin;
    GPIO_TypeDef *reset_port;
    uint16_t reset_pin;
} Max35103Stm32HalContext;

/**
 * Build a blocking STM32 HAL transport.
 *
 * hspi, NSS, and reset resources remain caller-owned and must outlive every
 * Max35103Driver instance using the returned transport. The adapter owns NSS
 * assertion/deassertion for each complete SPI transaction.
 */
Max35103Status MAX35103_Stm32HalInitTransport(
    Max35103Stm32HalContext *context,
    SPI_HandleTypeDef *hspi,
    GPIO_TypeDef *nss_port,
    uint16_t nss_pin,
    GPIO_TypeDef *reset_port,
    uint16_t reset_pin,
    Max35103Transport *transport);

#ifdef __cplusplus
}
#endif

#endif /* SWFPM_MAX35103_STM32_HAL_H */
