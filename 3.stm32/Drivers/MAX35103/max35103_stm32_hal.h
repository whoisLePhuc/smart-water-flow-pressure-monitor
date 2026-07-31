/**
  ******************************************************************************
  * @file    max35103_stm32_hal.h
  * @brief   STM32 HAL transport adapter for the portable MAX35103 driver
  *
  * @details
  * This adapter translates Max35103Transport operations into STM32 HAL SPI,
  * GPIO, tick, delay, and optional DMA calls. It owns the MAX35103 NSS pin for
  * the complete frame. STM32 SPI must be configured for mode 1 (CPOL=0,
  * CPHA=1), 8-bit data, MSB first, and software-managed NSS.
  *
  * Blocking transfers assert NSS, call HAL_SPI_TransmitReceive(), and release
  * NSS before returning. DMA transfers keep NSS asserted until
  * MAX35103_Stm32HalOnDmaIrq() records completion. The completion is then
  * forwarded to the portable FSM outside IRQ context.
  *
  * @warning One SPI handle may be shared only when the optional lock/unlock
  *          callbacks serialize the entire NSS-low transaction.
  ******************************************************************************
  */

#ifndef SWFPM_MAX35103_STM32_HAL_H
#define SWFPM_MAX35103_STM32_HAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "max35103.h"

/**
 * @brief Optional callback that acquires a shared STM32 SPI bus.
 * @param[in] context Application-defined mutex/semaphore context.
 * @param[in] timeout_ms Maximum acquisition time, in milliseconds.
 * @return Portable transport status.
 */
typedef Max35103TransportStatus (*Max35103Stm32HalBusLock)(
    void *context, uint32_t timeout_ms);

/**
 * @brief Optional callback that releases a previously acquired SPI bus.
 * @param[in] context Same application-defined context passed to the lock.
 */
typedef void (*Max35103Stm32HalBusUnlock)(void *context);

/**
 * @brief STM32 resources and DMA handoff state for one MAX35103 instance.
 *
 * The board composition layer owns every pointer stored here and must keep the
 * SPI handle, GPIO ports, and lock context alive for the complete driver
 * lifetime. DMA fields are written in IRQ context and consumed in the serialized
 * driver worker; volatile prevents compiler caching but is not an RTOS mutex.
 */
typedef struct {
    /** Borrowed STM32 SPI peripheral handle. */
    SPI_HandleTypeDef *hspi;
    /** GPIO port containing the active-low, software-controlled NSS pin. */
    GPIO_TypeDef *nss_port;
    /** Single-bit HAL GPIO mask for NSS. */
    uint16_t nss_pin;
    /** GPIO port containing the active-low MAX35103 reset pin. */
    GPIO_TypeDef *reset_port;
    /** Single-bit HAL GPIO mask for reset. */
    uint16_t reset_pin;
    /** Optional application-owned shared-bus acquisition callback. */
    Max35103Stm32HalBusLock bus_lock;
    /** Optional application-owned shared-bus release callback. */
    Max35103Stm32HalBusUnlock bus_unlock;
    /** Opaque object passed to bus_lock and bus_unlock. */
    void *bus_lock_context;
    /** Token of the currently active DMA request; zero means none. */
    volatile uint32_t dma_token;
    /** Token captured by the IRQ for deferred portable-core completion. */
    volatile uint32_t dma_completed_token;
    /** true from successful HAL DMA start until IRQ/cancel cleanup. */
    volatile bool dma_active;
    /** true when one IRQ completion awaits worker-context forwarding. */
    volatile bool dma_completion_pending;
    /** Success/failure evidence associated with dma_completed_token. */
    volatile bool dma_completion_ok;
} Max35103Stm32HalContext;

/**
 * @brief Build a blocking STM32 HAL transport.
 *
 * hspi, NSS, and reset resources remain caller-owned and must outlive every
 * Max35103Driver instance using the returned transport. The adapter owns NSS
 * assertion/deassertion for each complete SPI transaction.
 *
 * @param[out] context Adapter state to initialize.
 * @param[in] hspi Initialized STM32 HAL SPI handle.
 * @param[in] nss_port GPIO port used for MAX35103 NSS.
 * @param[in] nss_pin Nonzero single-pin HAL mask for NSS.
 * @param[in] reset_port GPIO port used for MAX35103 reset.
 * @param[in] reset_pin Nonzero single-pin HAL mask for reset.
 * @param[out] transport Portable transport table to populate.
 *
 * @retval MAX35103_OK Context and blocking transport were initialized.
 * @retval MAX35103_INVALID_ARG A pointer is NULL or a GPIO mask is zero.
 *
 * @pre SPI and GPIO peripherals are already initialized by board startup code.
 * @post Async hooks remain NULL until MAX35103_Stm32HalEnableDma() is called.
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
 * @brief Install optional shared-SPI-bus lock hooks.
 *
 * The hooks execute outside ISR context. lock() must honor timeout_ms and
 * unlock() must release the same mutex. Pass NULL for both hooks to disable
 * the platform mutex while retaining the core's transaction ownership guard.
 *
 * @param[in,out] context Initialized adapter context.
 * @param[in] lock Acquisition callback, or NULL together with unlock.
 * @param[in] unlock Release callback, or NULL together with lock.
 * @param[in] lock_context Opaque object passed to both callbacks.
 *
 * @retval MAX35103_OK Hook pair was installed.
 * @retval MAX35103_INVALID_ARG context is NULL or only one hook is NULL.
 * @retval MAX35103_BUSY DMA/completion handoff currently owns the context.
 */
Max35103Status MAX35103_Stm32HalSetBusLock(
    Max35103Stm32HalContext *context,
    Max35103Stm32HalBusLock lock,
    Max35103Stm32HalBusUnlock unlock,
    void *lock_context);

/**
 * @brief Enable deferred MAX35103 result reads through STM32 HAL SPI DMA.
 *
 * Call after Stm32HalInitTransport() and before MAX35103_Init(), because the
 * core copies Max35103Transport. Blocking configuration and diagnostics remain
 * available through transport.transfer.
 *
 * @param[in,out] context Initialized adapter context.
 * @param[in,out] transport Transport originally built from the same context.
 *
 * @retval MAX35103_OK Async start/cancel hooks were installed.
 * @retval MAX35103_INVALID_ARG Context/transport pairing is invalid.
 * @retval MAX35103_BUSY DMA/completion handoff is active.
 */
Max35103Status MAX35103_Stm32HalEnableDma(
    Max35103Stm32HalContext *context,
    Max35103Transport *transport);

/**
 * @brief Capture one DMA completion in IRQ context.
 *
 * Call from HAL_SPI_TxRxCpltCallback()/HAL_SPI_TxCpltCallback() with
 * transfer_ok=true, or from HAL_SPI_ErrorCallback() with false. This raises NSS
 * immediately but deliberately does not enter the portable driver's FSM.
 *
 * @param[in,out] context Adapter that owns the active DMA transfer.
 * @param[in] transfer_ok true for complete TX/TXRX; false for HAL SPI error.
 *
 * @post NSS is HIGH, dma_active is false, and one completion is pending.
 * @note NULL or an IRQ not associated with an active transfer is ignored.
 */
void MAX35103_Stm32HalOnDmaIrq(
    Max35103Stm32HalContext *context,
    bool transfer_ok);

/**
 * @brief Forward a captured DMA completion to the portable core.
 *
 * Call from the same deferred worker/main-loop context that owns
 * MAX35103_OnInt(), MAX35103_Process(), and result-queue consumption.
 *
 * @param[in,out] context Adapter containing IRQ-captured completion evidence.
 * @param[in,out] drv Portable driver that originated the DMA token.
 * @return true when one completion was consumed and forwarded; false when no
 *         completion was pending or an argument was invalid.
 */
bool MAX35103_Stm32HalProcessDmaCompletion(
    Max35103Stm32HalContext *context,
    Max35103Driver *drv);

/**
 * @brief Capture and immediately forward one DMA completion.
 *
 * Combined completion helper for simple bare-metal programs that serialize
 * every driver and queue operation. RTOS applications should use the split
 * OnDmaIrq()/ProcessDmaCompletion() path above.
 *
 * @param[in,out] context Adapter that owns the active DMA transfer.
 * @param[in,out] drv Portable driver that originated the DMA token.
 * @param[in] transfer_ok true for complete TX/TXRX; false for SPI error.
 *
 * @warning This helper enters the portable FSM from the caller's context.
 *          Do not call it directly from an IRQ when another task may access drv.
 */
void MAX35103_Stm32HalOnDmaComplete(
    Max35103Stm32HalContext *context,
    Max35103Driver *drv,
    bool transfer_ok);

#ifdef __cplusplus
}
#endif

#endif /* SWFPM_MAX35103_STM32_HAL_H */