#include "i2c_port_stm32.h"

#include <string.h>

/**
 * @brief Converts an adapter-independent STM32 HAL result to a port result.
 *
 * HAL errors are intentionally collapsed into PORT_STATUS_HARDWARE_ERROR so
 * upper layers do not depend on STM32-specific error values. A busy result is
 * preserved because callers may retry the request without recovering the bus.
 *
 * @param status Status returned by an operation in Stm32I2cHalOps.
 *
 * @return Equivalent status defined by the portable port contract.
 */
static PortStatus map_hal(Stm32AsyncHalStatus status)
{
    if (status == STM32_ASYNC_HAL_OK) return PORT_OK;
    if (status == STM32_ASYNC_HAL_BUSY) return PORT_STATUS_BUSY;
    return PORT_STATUS_HARDWARE_ERROR;
}

/**
 * @brief Submits one asynchronous I2C request to the STM32 HAL adapter.
 *
 * Only one request may be active per adapter. The request metadata is copied
 * after the HAL accepts it; the TX and RX buffers referenced by that copy
 * remain caller-owned and must stay valid until terminal completion.
 *
 * @param context Pointer to the Stm32I2cAdapter that owns the operation.
 * @param request Request to submit. The request object itself only needs to
 *                remain valid for the duration of this call.
 *
 * @return PORT_OK when the HAL accepts the request; PORT_STATUS_BUSY when the
 *         adapter or HAL is busy; PORT_STATUS_INVALID_PARAM for an invalid
 *         argument; otherwise PORT_STATUS_HARDWARE_ERROR.
 */
static PortStatus submit(void *context, const I2cPortRequest *request)
{
    Stm32I2cAdapter *adapter = context;
    if (!adapter || !request || adapter->active)
        return adapter && adapter->active ? PORT_STATUS_BUSY
                                          : PORT_STATUS_INVALID_PARAM;

    Stm32AsyncHalStatus status = adapter->ops->start(adapter->hal_i2c,
                                                      request);
    if (status != STM32_ASYNC_HAL_OK) return map_hal(status);

    /* Retain the accepted request identity for cancellation and completion. */
    adapter->active_request = *request;
    adapter->active = true;
    return PORT_OK;
}

/**
 * @brief Cancels the currently active I2C transaction when its identity matches.
 *
 * Both the transaction ID and bus generation are checked to prevent a stale
 * cancellation request from stopping a newer transaction that reused an ID.
 * The adapter remains active if the HAL does not confirm cancellation.
 *
 * @param context Pointer to the Stm32I2cAdapter that owns the operation.
 * @param transaction_id ID of the transaction to cancel.
 * @param bus_generation Bus generation in which the transaction was submitted.
 *
 * @return PORT_OK when cancellation succeeds; PORT_STATUS_INVALID_PARAM when
 *         no matching operation is active; otherwise the mapped HAL status.
 */
static PortStatus cancel(void *context, uint32_t transaction_id,
                         uint32_t bus_generation)
{
    Stm32I2cAdapter *adapter = context;
    if (!adapter || !adapter->active ||
        adapter->active_request.transaction_id != transaction_id ||
        adapter->active_request.bus_generation != bus_generation)
        return PORT_STATUS_INVALID_PARAM;

    PortStatus status = map_hal(adapter->ops->cancel(adapter->hal_i2c));
    if (status == PORT_OK) adapter->active = false;
    return status;
}

/**
 * @brief Requests hardware-level I2C bus recovery.
 *
 * Generation ownership is managed by the portable bus layer; this adapter only
 * resets the peripheral/bus through the supplied HAL operation. Successful
 * recovery invalidates any request previously tracked as active.
 *
 * @param context Pointer to the Stm32I2cAdapter to recover.
 * @param new_bus_generation Generation assigned by the bus manager after
 *                           recovery. It is not stored by this adapter.
 *
 * @return PORT_OK when recovery succeeds; PORT_STATUS_INVALID_PARAM when the
 *         adapter is NULL; otherwise the mapped HAL status.
 */
static PortStatus recover(void *context, uint32_t new_bus_generation)
{
    Stm32I2cAdapter *adapter = context;
    (void)new_bus_generation;
    if (!adapter) return PORT_STATUS_INVALID_PARAM;

    PortStatus status = map_hal(adapter->ops->recover(adapter->hal_i2c));
    if (status == PORT_OK) adapter->active = false;
    return status;
}

/**
 * @brief Initializes an STM32 I2C adapter and exposes it as an I2cPort.
 *
 * The HAL handle, operation table, and completion context remain caller-owned
 * and must outlive the initialized adapter. Initialization clears all runtime
 * state, so the adapter starts with no active request.
 *
 * @param adapter Adapter instance to initialize.
 * @param hal_i2c Board-specific STM32 HAL I2C handle.
 * @param ops HAL operation table used to start, cancel, and recover transfers.
 * @param sink Completion callback invoked for each active terminal result.
 * @param sink_context Caller-owned context passed to @p sink.
 * @param port_out Output portable I2C port bound to @p adapter.
 *
 * @return PORT_OK on success, or PORT_STATUS_INVALID_PARAM when a required
 *         pointer or HAL operation is missing.
 */
PortStatus i2c_port_stm32_init(Stm32I2cAdapter *adapter,
                               void *hal_i2c,
                               const Stm32I2cHalOps *ops,
                               Stm32I2cCompletionSink sink,
                               void *sink_context,
                               I2cPort *port_out)
{
    if (!adapter || !hal_i2c || !ops || !ops->start || !ops->cancel ||
        !ops->recover || !sink || !port_out)
        return PORT_STATUS_INVALID_PARAM;

    memset(adapter, 0, sizeof(*adapter));
    adapter->hal_i2c = hal_i2c;
    adapter->ops = ops;
    adapter->completion_sink = sink;
    adapter->completion_context = sink_context;

    port_out->context = adapter;
    port_out->submit = submit;
    port_out->cancel = cancel;
    port_out->recover = recover;
    return PORT_OK;
}

/**
 * @brief Completes the active request and forwards its result to the sink.
 *
 * Board integration code calls this function after leaving the peripheral IRQ
 * handler or from a deferred IRQ callback. The active flag is cleared before
 * invoking user code, allowing the completion sink to submit the next request
 * safely. Calls received with no active request are ignored, which prevents a
 * late hardware callback from producing a duplicate completion.
 *
 * The request pointer passed to the sink refers to a temporary local copy and
 * is valid only for the duration of the callback. This function does not
 * directly mutate an application event queue.
 *
 * @param adapter Adapter that owns the completed request.
 * @param result Portable terminal result reported by the board integration.
 */
void i2c_port_stm32_on_complete(Stm32I2cAdapter *adapter,
                                PortStatus result)
{
    if (!adapter || !adapter->active) return;

    /* Copy before clearing active state so callback re-entry cannot overwrite
     * the identity of the operation being completed. */
    I2cPortRequest completed = adapter->active_request;
    adapter->active = false;
    adapter->completion_sink(adapter->completion_context, &completed, result);
}
