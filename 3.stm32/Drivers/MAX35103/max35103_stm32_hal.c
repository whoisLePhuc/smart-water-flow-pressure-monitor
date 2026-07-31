/**
  ******************************************************************************
  * @file    max35103_stm32_hal.c
  * @brief   STM32 HAL transport adapter for the portable MAX35103 driver
  *
  * @details
  * NSS is controlled as an ordinary GPIO because the MAX35103 requires one
  * uninterrupted chip-select interval for an opcode and every following data
  * byte. Hardware NSS pulse mode must therefore not split a sequential read.
  *
  * DMA completion uses a two-stage handoff:
  * 1. The HAL IRQ callback releases NSS and records token/status only.
  * 2. The main-loop/worker forwards that evidence into MAX35103_OnSpiDone().
  *
  * The portable core, not this adapter, owns the shared-bus lock lifetime.
  * Consequently a DMA transfer may keep the lock held after the IRQ until the
  * deferred completion reaches the core.
  ******************************************************************************
  */

#include "max35103_stm32_hal.h"

#include <string.h>

/**
 * @brief Map STM32 HAL status codes into the portable transport domain.
 *
 * HAL_ERROR and any future/unrecognized HAL status intentionally collapse to
 * MAX35103_TRANSPORT_ERROR because the core only distinguishes success, busy,
 * timeout, and generic transport failure.
 */
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

/**
 * @brief Bridge the core's optional bus-lock request to the board callback.
 *
 * An absent callback means the SPI peripheral is dedicated or the application
 * guarantees serialization by design. The core still maintains its own
 * spi_bus_locked guard for reentrancy.
 */
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

/** @brief Release the application-owned shared-SPI mutex, when installed. */
static void max35103_stm32_unlock(void *context)
{
    Max35103Stm32HalContext *hal =
        (Max35103Stm32HalContext *)context;
    if (hal != NULL && hal->bus_unlock != NULL) {
        hal->bus_unlock(hal->bus_lock_context);
    }
}

/**
 * @brief Execute one complete blocking MAX35103 SPI transaction.
 *
 * rx==NULL selects transmit-only mode for execution commands. Register reads
 * use full-duplex mode because the MAX35103 returns data while dummy bytes are
 * transmitted. NSS is restored HIGH on every HAL return path, including
 * HAL_TIMEOUT and HAL_ERROR.
 */
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

    /*
     * Assert NSS before the first opcode bit and keep it low through the final
     * data bit. This is essential for the 71-byte TOF result-bank read.
     */
    HAL_GPIO_WritePin(hal->nss_port, hal->nss_pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef status;
    if (rx != NULL) {
        status = HAL_SPI_TransmitReceive(
            hal->hspi, (uint8_t *)tx, rx, length, timeout_ms);
    } else {
        status = HAL_SPI_Transmit(
            hal->hspi, (uint8_t *)tx, length, timeout_ms);
    }
    /* Always release the device before mapping/returning the HAL result. */
    HAL_GPIO_WritePin(hal->nss_port, hal->nss_pin, GPIO_PIN_SET);
    return max35103_stm32_map_hal(status);
}

/**
 * @brief Start one complete SPI frame through STM32 HAL DMA.
 *
 * The token is stored before DMA begins so even a very fast completion IRQ can
 * identify the request. dma_completion_pending also blocks reuse: the old IRQ
 * evidence must reach the portable core before these staging fields can be
 * overwritten by a new transaction.
 */
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

    /*
     * Publish token/active state before enabling DMA. On Cortex-M, the HAL call
     * may enable interrupts and completion can occur immediately afterwards.
     */
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
        /*
         * No completion IRQ is guaranteed after a failed start. Perform the
         * entire NSS/state rollback synchronously so the bus cannot remain
         * selected or falsely busy.
         */
        HAL_GPIO_WritePin(hal->nss_port, hal->nss_pin, GPIO_PIN_SET);
        hal->dma_active = false;
        hal->dma_token = 0U;
    }
    return max35103_stm32_map_hal(status);
}

/**
 * @brief Cancel an active or IRQ-completed asynchronous transaction.
 *
 * There are two valid cancellation windows:
 * - The IRQ already completed and only its deferred handoff is pending. Clear
 *   that stale handoff without aborting an idle HAL peripheral.
 * - DMA is still active with the same token. Abort HAL SPI and release NSS.
 *
 * A token mismatch is rejected so a timeout from an older FSM generation
 * cannot cancel a newer transfer.
 */
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
        /* IRQ already raised NSS; only deferred completion evidence remains. */
        hal->dma_completion_pending = false;
        hal->dma_completion_ok = false;
        hal->dma_completed_token = 0U;
        return MAX35103_TRANSPORT_OK;
    }
    if (!hal->dma_active || token != hal->dma_token) {
        return MAX35103_TRANSPORT_ERROR;
    }

    /*
     * HAL_SPI_Abort() is used rather than waiting for DMA completion because
     * the portable core calls this path specifically during timeout/cancel
     * recovery.
     */
    const HAL_StatusTypeDef status = HAL_SPI_Abort(hal->hspi);
    HAL_GPIO_WritePin(hal->nss_port, hal->nss_pin, GPIO_PIN_SET);
    hal->dma_active = false;
    hal->dma_token = 0U;
    return max35103_stm32_map_hal(status);
}

/**
 * @brief Drive the MAX35103 active-low hardware reset pin.
 *
 * The portable API expresses logical assertion, so asserted=true maps to a
 * physical RESET level and asserted=false maps to SET.
 */
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

/** @brief Return the STM32 HAL millisecond tick used by blocking deadlines. */
static uint32_t max35103_stm32_get_tick_ms(void *context)
{
    (void)context;
    return HAL_GetTick();
}

/** @brief Bridge the core's blocking millisecond delay to HAL_Delay(). */
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

    /*
     * Clear stale DMA completion evidence before publishing any borrowed
     * hardware resources. Reinitialization is only safe while DMA is inactive.
     */
    memset(context, 0, sizeof(*context));
    context->hspi = hspi;
    context->nss_port = nss_port;
    context->nss_pin = nss_pin;
    context->reset_port = reset_port;
    context->reset_pin = reset_pin;

    /*
     * Start in blocking mode. DMA hooks are installed explicitly later so a
     * board can use the same adapter with or without configured DMA channels.
     */
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

    /*
     * Hooks are replaced only while no DMA ownership/completion spans the
     * change, preventing a transaction from acquiring with one pair and
     * releasing through another.
     */
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

    /*
     * The portable driver copies this table in MAX35103_Init(); installing the
     * hooks after that copy would not enable DMA in the existing driver.
     */
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

    /*
     * NSS must be HIGH before portable completion can release the shared bus
     * or schedule the next transaction. Keep the IRQ path bounded: no register
     * decode, queue operation, or state-machine traversal occurs here.
     */
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

    /*
     * Snapshot the volatile IRQ fields, clear the single-entry handoff slot,
     * then enter the portable FSM. New DMA cannot legally start until the core
     * consumes this completion and releases its bus ownership.
     */
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
    /*
     * Convenience path for strictly serialized bare-metal firmware. It is not
     * the preferred RTOS IRQ path because OnSpiDone() mutates the driver FSM.
     */
    MAX35103_Stm32HalOnDmaIrq(context, transfer_ok);
    (void)MAX35103_Stm32HalProcessDmaCompletion(context, drv);
}