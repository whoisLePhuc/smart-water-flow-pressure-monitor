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

static Max35103TransportStatus max35103_stm32_lock(
    void *context, uint32_t timeout_ms)
{
    Max35103Stm32HalContext *hal =
        (Max35103Stm32HalContext *)context;
    if (hal == NULL) {
        return MAX35103_TRANSPORT_ERROR;
    }
    if (hal->bus_lock == NULL) {
        return MAX35103_TRANSPORT_OK;
    }
    return hal->bus_lock(hal->bus_lock_context, timeout_ms);
}

static void max35103_stm32_unlock(void *context)
{
    Max35103Stm32HalContext *hal =
        (Max35103Stm32HalContext *)context;
    if (hal != NULL && hal->bus_unlock != NULL) {
        hal->bus_unlock(hal->bus_lock_context);
    }
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

static Max35103TransportStatus max35103_stm32_start_transfer_async(
    void *context, const uint8_t *tx, uint8_t *rx,
    uint16_t length, uint32_t token)
{
    Max35103Stm32HalContext *hal =
        (Max35103Stm32HalContext *)context;
    if (hal == NULL || hal->hspi == NULL ||
        hal->nss_port == NULL || hal->nss_pin == 0U ||
        tx == NULL || length == 0U || token == 0U ||
        hal->dma_active || hal->dma_completion_pending) {
        return hal != NULL &&
               (hal->dma_active || hal->dma_completion_pending)
               ? MAX35103_TRANSPORT_BUSY
               : MAX35103_TRANSPORT_ERROR;
    }

    hal->dma_token = token;
    hal->dma_active = true;
    HAL_GPIO_WritePin(hal->nss_port, hal->nss_pin, GPIO_PIN_RESET);

    HAL_StatusTypeDef status;
    if (rx != NULL) {
        status = HAL_SPI_TransmitReceive_DMA(
            hal->hspi, (uint8_t *)tx, rx, length);
    } else {
        status = HAL_SPI_Transmit_DMA(
            hal->hspi, (uint8_t *)tx, length);
    }

    if (status != HAL_OK) {
        HAL_GPIO_WritePin(hal->nss_port, hal->nss_pin, GPIO_PIN_SET);
        hal->dma_active = false;
        hal->dma_token = 0U;
    }
    return max35103_stm32_map_hal(status);
}

static Max35103TransportStatus max35103_stm32_cancel_transfer_async(
    void *context, uint32_t token)
{
    Max35103Stm32HalContext *hal =
        (Max35103Stm32HalContext *)context;
    if (hal == NULL || hal->hspi == NULL || token == 0U) {
        return MAX35103_TRANSPORT_ERROR;
    }

    if (hal->dma_completion_pending &&
        token == hal->dma_completed_token) {
        hal->dma_completion_pending = false;
        hal->dma_completion_ok = false;
        hal->dma_completed_token = 0U;
        return MAX35103_TRANSPORT_OK;
    }
    if (!hal->dma_active || token != hal->dma_token) {
        return MAX35103_TRANSPORT_ERROR;
    }

    const HAL_StatusTypeDef status = HAL_SPI_Abort(hal->hspi);
    HAL_GPIO_WritePin(hal->nss_port, hal->nss_pin, GPIO_PIN_SET);
    hal->dma_active = false;
    hal->dma_token = 0U;
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
    transport->lock = max35103_stm32_lock;
    transport->unlock = max35103_stm32_unlock;
    transport->set_reset = max35103_stm32_set_reset;
    transport->get_tick_ms = max35103_stm32_get_tick_ms;
    transport->delay_ms = max35103_stm32_delay_ms;
    transport->context = context;
    return MAX35103_OK;
}

Max35103Status MAX35103_Stm32HalSetBusLock(
    Max35103Stm32HalContext *context,
    Max35103Stm32HalBusLock lock,
    Max35103Stm32HalBusUnlock unlock,
    void *lock_context)
{
    if (context == NULL ||
        ((lock == NULL) != (unlock == NULL))) {
        return MAX35103_INVALID_ARG;
    }
    if (context->dma_active || context->dma_completion_pending) {
        return MAX35103_BUSY;
    }

    context->bus_lock = lock;
    context->bus_unlock = unlock;
    context->bus_lock_context = lock_context;
    return MAX35103_OK;
}

Max35103Status MAX35103_Stm32HalEnableDma(
    Max35103Stm32HalContext *context,
    Max35103Transport *transport)
{
    if (context == NULL || transport == NULL ||
        transport->context != context ||
        transport->transfer != max35103_stm32_transfer ||
        context->hspi == NULL) {
        return MAX35103_INVALID_ARG;
    }
    if (context->dma_active || context->dma_completion_pending) {
        return MAX35103_BUSY;
    }

    transport->start_transfer_async =
        max35103_stm32_start_transfer_async;
    transport->cancel_transfer_async =
        max35103_stm32_cancel_transfer_async;
    return MAX35103_OK;
}

void MAX35103_Stm32HalOnDmaIrq(
    Max35103Stm32HalContext *context,
    bool transfer_ok)
{
    if (context == NULL || !context->dma_active) {
        return;
    }

    HAL_GPIO_WritePin(
        context->nss_port, context->nss_pin, GPIO_PIN_SET);
    context->dma_completed_token = context->dma_token;
    context->dma_completion_ok = transfer_ok;
    context->dma_completion_pending = true;
    context->dma_active = false;
    context->dma_token = 0U;
}

bool MAX35103_Stm32HalProcessDmaCompletion(
    Max35103Stm32HalContext *context,
    Max35103Driver *drv)
{
    if (context == NULL || drv == NULL ||
        !context->dma_completion_pending) {
        return false;
    }

    const uint32_t token = context->dma_completed_token;
    const bool transfer_ok = context->dma_completion_ok;
    context->dma_completion_pending = false;
    context->dma_completion_ok = false;
    context->dma_completed_token = 0U;
    MAX35103_OnSpiDone(drv, token, transfer_ok);
    return true;
}

void MAX35103_Stm32HalOnDmaComplete(
    Max35103Stm32HalContext *context,
    Max35103Driver *drv,
    bool transfer_ok)
{
    MAX35103_Stm32HalOnDmaIrq(context, transfer_ok);
    (void)MAX35103_Stm32HalProcessDmaCompletion(context, drv);
}
