#ifndef SWFPM_I2C_PORT_STM32_H
#define SWFPM_I2C_PORT_STM32_H

#include <stdbool.h>

#include "ports/i2c_port.h"

/**
 * @brief Result returned by the board-specific asynchronous I2C HAL wrapper.
 *
 * These values describe whether a HAL operation was started or performed, not
 * the final result of an accepted asynchronous I2C transaction.
 */
typedef enum {
    STM32_ASYNC_HAL_OK,   /**< The HAL operation was accepted or completed. */
    STM32_ASYNC_HAL_BUSY, /**< The HAL cannot accept the operation now. */
    STM32_ASYNC_HAL_ERROR /**< The HAL rejected the operation due to an error. */
} Stm32AsyncHalStatus;

/**
 * @brief Board-specific operations used by the portable STM32 I2C adapter.
 *
 * The adapter owns neither this table nor the HAL I2C object passed to these
 * functions. Both must remain valid for the entire adapter lifetime.
 */
typedef struct {
    /**
     * @brief Starts an asynchronous I2C transaction.
     *
     * The implementation must not retain the request pointer itself after the
     * call returns. The buffers referenced by the request remain caller-owned
     * and valid until terminal completion.
     *
     * @param hal_i2c Board-specific HAL I2C instance.
     * @param request Transaction to start.
     *
     * @return HAL submission status.
     */
    Stm32AsyncHalStatus (*start)(void* hal_i2c, const I2cPortRequest* request);

    /**
     * @brief Requests cancellation of the active HAL transaction.
     *
     * @param hal_i2c Board-specific HAL I2C instance.
     *
     * @return HAL cancellation status.
     */
    Stm32AsyncHalStatus (*cancel)(void* hal_i2c);

    /**
     * @brief Attempts to restore the I2C peripheral and bus to an idle state.
     *
     * Recovery may reset or reinitialize the peripheral according to the board
     * implementation. The adapter must not have an active transaction when a
     * new request is submitted after recovery.
     *
     * @param hal_i2c Board-specific HAL I2C instance.
     *
     * @return HAL recovery status.
     */
    Stm32AsyncHalStatus (*recover)(void* hal_i2c);
} Stm32I2cHalOps;

/**
 * @brief Receives the terminal result of an STM32 I2C transaction.
 *
 * The request points to the adapter-owned snapshot of the accepted request and
 * is valid only for the duration of the callback. The callback must copy any
 * information that it needs to retain.
 *
 * @param context Consumer-provided callback context.
 * @param request Completed request snapshot.
 * @param result Terminal transaction result.
 */
typedef void (*Stm32I2cCompletionSink)(void* context,
                                       const I2cPortRequest* request,
                                       PortStatus result);

/**
 * @brief Instance state for the STM32 implementation of I2cPort.
 *
 * One adapter supports at most one active transaction. The structure is
 * instance-owned and must remain valid while its exported I2cPort can be used
 * or while a transaction is active.
 */
typedef struct {
    void* hal_i2c; /**< Borrowed board-specific HAL handle. */

    /** Borrowed HAL operation table; must outlive the adapter. */
    const Stm32I2cHalOps* ops;

    /** Consumer notified when an accepted transaction reaches completion. */
    Stm32I2cCompletionSink completion_sink;

    /** Opaque context passed unchanged to completion_sink. */
    void* completion_context;

    /**
     * Snapshot of the currently accepted request.
     *
     * Pointer fields inside the snapshot remain caller-owned; their referenced
     * buffers must stay valid until the terminal completion is delivered.
     */
    I2cPortRequest active_request;

    /** True while active_request represents an in-flight transaction. */
    bool active;
} Stm32I2cAdapter;

/**
 * @brief Initializes an STM32 I2C adapter and exports its I2cPort interface.
 *
 * The HAL handle, operation table, adapter, and output port must remain valid
 * for the required usage lifetime. The completion sink is called once when an
 * accepted transaction reaches terminal completion.
 *
 * @param adapter Adapter instance to initialize.
 * @param hal_i2c Borrowed board-specific HAL I2C handle.
 * @param ops Borrowed HAL operation table.
 * @param sink Completion consumer for accepted transactions.
 * @param sink_context Opaque context passed to sink.
 * @param port_out Output receiving the initialized I2cPort interface.
 *
 * @return Initialization result.
 */
PortStatus i2c_port_stm32_init(Stm32I2cAdapter* adapter,
                               void* hal_i2c,
                               const Stm32I2cHalOps* ops,
                               Stm32I2cCompletionSink sink,
                               void* sink_context,
                               I2cPort* port_out);

/**
 * @brief Completes the adapter's active asynchronous I2C transaction.
 *
 * The board integration calls this function after leaving the peripheral IRQ
 * handler or from a deferred IRQ callback. The function finalizes adapter
 * state and forwards the result to the configured completion sink; it does not
 * directly mutate an application event queue.
 *
 * Calling this function without an active transaction has no valid completion
 * to deliver and should be avoided by the board integration.
 *
 * @param adapter Adapter that owns the active transaction.
 * @param result Terminal status reported by the board HAL integration.
 */
void i2c_port_stm32_on_complete(Stm32I2cAdapter* adapter, PortStatus result);

#endif /* SWFPM_I2C_PORT_STM32_H */
