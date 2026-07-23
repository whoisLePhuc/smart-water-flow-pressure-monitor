/**
  ******************************************************************************
  * @file    zssc3241_stm32_hal.c
  * @brief   STM32 HAL transport adapter for the ZSSC3241 core driver
  ******************************************************************************
  */

#include "zssc3241_stm32_hal.h"

#include <string.h>

static Zssc3241TransportStatus zssc_stm32_map_hal(
    I2C_HandleTypeDef *hi2c, HAL_StatusTypeDef status)
{
    if (status == HAL_OK) {
        return ZSSC3241_TRANSPORT_OK;
    }
    if (status == HAL_BUSY) {
        return ZSSC3241_TRANSPORT_BUSY;
    }
    if (status == HAL_TIMEOUT) {
        return ZSSC3241_TRANSPORT_TIMEOUT;
    }
#if defined(HAL_I2C_ERROR_AF)
    if (hi2c != NULL &&
        (HAL_I2C_GetError(hi2c) & HAL_I2C_ERROR_AF) != 0U) {
        return ZSSC3241_TRANSPORT_NACK;
    }
#else
    (void)hi2c;
#endif
    return ZSSC3241_TRANSPORT_ERROR;
}

static Zssc3241TransportStatus zssc_stm32_write(
    void *context, uint8_t address_7bit,
    const uint8_t *data, uint16_t length,
    uint32_t timeout_ms)
{
    Zssc3241Stm32HalContext *hal = (Zssc3241Stm32HalContext *)context;
    if (hal == NULL || hal->hi2c == NULL || data == NULL || length == 0U) {
        return ZSSC3241_TRANSPORT_ERROR;
    }

    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(
        hal->hi2c, (uint16_t)((uint16_t)address_7bit << 1),
        (uint8_t *)data, length, timeout_ms);
    return zssc_stm32_map_hal(hal->hi2c, status);
}

static Zssc3241TransportStatus zssc_stm32_read(
    void *context, uint8_t address_7bit,
    uint8_t *data, uint16_t length,
    uint32_t timeout_ms)
{
    Zssc3241Stm32HalContext *hal = (Zssc3241Stm32HalContext *)context;
    if (hal == NULL || hal->hi2c == NULL || data == NULL || length == 0U) {
        return ZSSC3241_TRANSPORT_ERROR;
    }

    HAL_StatusTypeDef status = HAL_I2C_Master_Receive(
        hal->hi2c, (uint16_t)((uint16_t)address_7bit << 1),
        data, length, timeout_ms);
    return zssc_stm32_map_hal(hal->hi2c, status);
}

static uint32_t zssc_stm32_get_tick(void *context)
{
    (void)context;
    return HAL_GetTick();
}

static void zssc_stm32_delay(void *context, uint32_t delay_ms)
{
    (void)context;
    HAL_Delay(delay_ms);
}

static Zssc3241TransportStatus zssc_stm32_set_reset(
    void *context, bool asserted)
{
    Zssc3241Stm32HalContext *hal =
        (Zssc3241Stm32HalContext *)context;
    if (hal == NULL || hal->reset_port == NULL || hal->reset_pin == 0U) {
        return ZSSC3241_TRANSPORT_ERROR;
    }

    HAL_GPIO_WritePin(hal->reset_port, hal->reset_pin,
                      asserted ? GPIO_PIN_RESET : GPIO_PIN_SET);
    return ZSSC3241_TRANSPORT_OK;
}

Zssc3241Status ZSSC3241_Stm32HalInitTransport(
    Zssc3241Stm32HalContext *context,
    I2C_HandleTypeDef *hi2c,
    GPIO_TypeDef *reset_port,
    uint16_t reset_pin,
    Zssc3241Transport *transport)
{
    if (context == NULL || hi2c == NULL || transport == NULL) {
        return ZSSC3241_INVALID_ARG;
    }

    memset(context, 0, sizeof(*context));
    context->hi2c = hi2c;
    context->reset_port = reset_port;
    context->reset_pin = reset_pin;

    memset(transport, 0, sizeof(*transport));
    transport->write = zssc_stm32_write;
    transport->read = zssc_stm32_read;
    transport->get_tick_ms = zssc_stm32_get_tick;
    transport->delay_ms = zssc_stm32_delay;
    transport->set_reset = reset_port != NULL && reset_pin != 0U
        ? zssc_stm32_set_reset : NULL;
    transport->context = context;
    return ZSSC3241_OK;
}
