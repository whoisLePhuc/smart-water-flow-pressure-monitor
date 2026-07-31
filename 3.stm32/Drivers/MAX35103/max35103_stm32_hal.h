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

typedef Max35103TransportStatus (*Max35103Stm32HalBusLock)(
    void *context, uint32_t timeout_ms);
typedef void (*Max35103Stm32HalBusUnlock)(void *context);

/** STM32 resources owned by the board composition layer. */
typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *nss_port;
    uint16_t nss_pin;
    GPIO_TypeDef *reset_port;
    uint16_t reset_pin;
    Max35103Stm32HalBusLock bus_lock;
    Max35103Stm32HalBusUnlock bus_unlock;
    void *bus_lock_context;
    volatile uint32_t dma_token;
    volatile uint32_t dma_completed_token;
    volatile bool dma_active;
    volatile bool dma_completion_pending;
    volatile bool dma_completion_ok;
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

/**
 * Install optional shared-SPI-bus lock hooks.
 *
 * The hooks execute outside ISR context. lock() must honor timeout_ms and
 * unlock() must release the same mutex. Pass NULL for both hooks to disable
 * the platform mutex while retaining the core's transaction ownership guard.
 */
Max35103Status MAX35103_Stm32HalSetBusLock(
    Max35103Stm32HalContext *context,
    Max35103Stm32HalBusLock lock,
    Max35103Stm32HalBusUnlock unlock,
    void *lock_context);

/**
 * Enable deferred MAX35103 result reads through STM32 HAL SPI DMA.
 *
 * Call after Stm32HalInitTransport() and before MAX35103_Init(), because the
 * core copies Max35103Transport. Blocking configuration and diagnostics remain
 * available through transport.transfer.
 */
Max35103Status MAX35103_Stm32HalEnableDma(
    Max35103Stm32HalContext *context,
    Max35103Transport *transport);

/**
 * Capture one DMA completion in IRQ context.
 *
 * Call from HAL_SPI_TxRxCpltCallback()/HAL_SPI_TxCpltCallback() with
 * transfer_ok=true, or from HAL_SPI_ErrorCallback() with false. This raises NSS
 * immediately but deliberately does not enter the portable driver's FSM.
 */
void MAX35103_Stm32HalOnDmaIrq(
    Max35103Stm32HalContext *context,
    bool transfer_ok);

/**
 * Forward a captured DMA completion to the portable core.
 *
 * Call from the same deferred worker/main-loop context that owns
 * MAX35103_OnInt(), MAX35103_Process(), and result-queue consumption.
 */
bool MAX35103_Stm32HalProcessDmaCompletion(
    Max35103Stm32HalContext *context,
    Max35103Driver *drv);

/**
 * Combined completion helper for simple bare-metal programs that serialize
 * every driver and queue operation. RTOS applications should use the split
 * OnDmaIrq()/ProcessDmaCompletion() path above.
 */
void MAX35103_Stm32HalOnDmaComplete(
    Max35103Stm32HalContext *context,
    Max35103Driver *drv,
    bool transfer_ok);

#ifdef __cplusplus
}
#endif

#endif /* SWFPM_MAX35103_STM32_HAL_H */
