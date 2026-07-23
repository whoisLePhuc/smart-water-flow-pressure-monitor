/**
  ******************************************************************************
  * @file    max35103_stm32_hal.c
  * @brief   STM32 HAL transport adapter for the portable MAX35103 driver
  ******************************************************************************
  */

#include "max35103_stm32_hal.h"

#include <string.h>

static Max35103TransportStatus max35103_stm32_map_hal(
    HAL_StatusTypeDef status)
{
    if (status == HAL_OK) {
        return MAX35103_TRANSPORT_OK;
    }
    if (status == HAL_BUSY) {
        return MAX35103_TRANSPORT_BUSY;
    }
    if (status == HAL_TIMEOUT) {
        return MAX35103_TRANSPORT_TIMEOUT;
    }
    return MAX35103_TRANSPORT_ERROR;
}

static Max35103TransportStatus max35103_stm32_transfer(
    void *context, const uint8_t *tx, uint8_t *rx,
    uint16_t length, uint32_t timeout_ms)
{
    Max35103Stm32HalContext *hal =
        (Max35103Stm32HalContext *)context;
    if (hal == NULL || hal->hspi == NULL ||
        hal->nss_port == NULL || hal->nss_pin == 0U ||
        tx == NULL || length == 0U) {
        return MAX35103_TRANSPORT_ERROR;
    }

    HAL_GPIO_WritePin(hal->nss_port, hal->nss_pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef status;
    if (rx != NULL) {
        status = HAL_SPI_TransmitReceive(
            hal->hspi, (uint8_t *)tx, rx, length, timeout_ms);
    } else {
        status = HAL_SPI_Transmit(
            hal->hspi, (uint8_t *)tx, length, timeout_ms);
    }
    HAL_GPIO_WritePin(hal->nss_port, hal->nss_pin, GPIO_PIN_SET);
    return max35103_stm32_map_hal(status);
}

static Max35103TransportStatus max35103_stm32_set_reset(
    void *context, bool asserted)
{
    Max35103Stm32HalContext *hal =
        (Max35103Stm32HalContext *)context;
    if (hal == NULL || hal->reset_port == NULL ||
        hal->reset_pin == 0U) {
        return MAX35103_TRANSPORT_ERROR;
    }

    HAL_GPIO_WritePin(
        hal->reset_port, hal->reset_pin,
        asserted ? GPIO_PIN_RESET : GPIO_PIN_SET);
    return MAX35103_TRANSPORT_OK;
}

static uint32_t max35103_stm32_get_tick_ms(void *context)
{
    (void)context;
    return HAL_GetTick();
}

static void max35103_stm32_delay_ms(
    void *context, uint32_t delay_ms)
{
    (void)context;
    HAL_Delay(delay_ms);
}

Max35103Status MAX35103_Stm32HalInitTransport(
    Max35103Stm32HalContext *context,
    SPI_HandleTypeDef *hspi,
    GPIO_TypeDef *nss_port,
    uint16_t nss_pin,
    GPIO_TypeDef *reset_port,
    uint16_t reset_pin,
    Max35103Transport *transport)
{
    if (context == NULL || hspi == NULL ||
        nss_port == NULL || nss_pin == 0U ||
        reset_port == NULL || reset_pin == 0U ||
        transport == NULL) {
        return MAX35103_INVALID_ARG;
    }

    memset(context, 0, sizeof(*context));
    context->hspi = hspi;
    context->nss_port = nss_port;
    context->nss_pin = nss_pin;
    context->reset_port = reset_port;
    context->reset_pin = reset_pin;

    memset(transport, 0, sizeof(*transport));
    transport->transfer = max35103_stm32_transfer;
    transport->set_reset = max35103_stm32_set_reset;
    transport->get_tick_ms = max35103_stm32_get_tick_ms;
    transport->delay_ms = max35103_stm32_delay_ms;
    transport->context = context;
    return MAX35103_OK;
}
