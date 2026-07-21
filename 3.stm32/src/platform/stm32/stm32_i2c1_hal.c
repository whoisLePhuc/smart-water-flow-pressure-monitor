#include "stm32_i2c1_hal.h"

#include <string.h>

/*
 * STM32 HAL callbacks do not provide application context, so this pointer
 * routes I2C1 callbacks to the instance-owned state.
 */
static Stm32I2c1Hal *s_i2c1;

static uint32_t critical_enter(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void critical_exit(uint32_t primask)
{
    if ((primask & 1u) == 0u)
        __enable_irq();
}

static Stm32AsyncHalStatus map_hal_status(
    HAL_StatusTypeDef status)
{
    if (status == HAL_OK)
        return STM32_ASYNC_HAL_OK;

    if (status == HAL_BUSY)
        return STM32_ASYNC_HAL_BUSY;

    return STM32_ASYNC_HAL_ERROR;
}

static bool request_is_valid(
    const I2cPortRequest *request)
{
    return request &&
           request->slave_address <= 0x7fu &&
           (request->tx_length == 0u || request->tx) &&
           (request->rx_length == 0u || request->rx) &&
           (request->tx_length != 0u ||
            request->rx_length != 0u);
}

static void clear_transfer_state(
    Stm32I2c1Hal *context)
{
    context->device_address = 0u;
    context->rx_buffer = NULL;
    context->rx_length = 0u;
    context->phase = STM32_I2C1_PHASE_IDLE;
    context->transfer_active = false;
    context->completion_pending = false;
    context->completion_result =
        PORT_STATUS_HARDWARE_ERROR;
}

static void latch_completion(
    Stm32I2c1Hal *context,
    PortStatus result)
{
    if (!context ||
        !context->transfer_active ||
        context->completion_pending) {
        return;
    }

    context->transfer_active = false;
    context->phase = STM32_I2C1_PHASE_IDLE;
    context->completion_result = result;
    context->completion_pending = true;
}

static Stm32AsyncHalStatus start(
    void *hal_context,
    const I2cPortRequest *request)
{
    Stm32I2c1Hal *context = hal_context;

    if (!context ||
        !context->handle ||
        !request_is_valid(request)) {
        return STM32_ASYNC_HAL_ERROR;
    }

    if (context->transfer_active ||
        context->completion_pending ||
        HAL_I2C_GetState(context->handle) !=
            HAL_I2C_STATE_READY) {
        return STM32_ASYNC_HAL_BUSY;
    }

    /*
     * I2cPort uses a 7-bit address. STM32 HAL expects the
     * address shifted left by one bit.
     */
    context->device_address =
        (uint16_t)request->slave_address << 1u;

    context->rx_buffer = request->rx;
    context->rx_length = request->rx_length;
    context->last_hal_error = HAL_I2C_ERROR_NONE;
    context->completion_result =
        PORT_STATUS_HARDWARE_ERROR;
    context->transfer_active = true;

    HAL_StatusTypeDef status;

    if (request->tx_length != 0u &&
        request->rx_length != 0u) {
        /*
         * Combined transfer:
         *
         * START → address(W) → TX
         * REPEATED START → address(R) → RX → STOP
         */
        context->phase =
            STM32_I2C1_PHASE_TX_THEN_RX_TX;

        status = HAL_I2C_Master_Seq_Transmit_IT(
            context->handle,
            context->device_address,
            (uint8_t *)request->tx,
            request->tx_length,
            I2C_FIRST_FRAME);
    } else if (request->tx_length != 0u) {
        context->phase = STM32_I2C1_PHASE_TX_ONLY;

        status = HAL_I2C_Master_Transmit_IT(
            context->handle,
            context->device_address,
            (uint8_t *)request->tx,
            request->tx_length);
    } else {
        context->phase = STM32_I2C1_PHASE_RX_ONLY;

        status = HAL_I2C_Master_Receive_IT(
            context->handle,
            context->device_address,
            request->rx,
            request->rx_length);
    }

    if (status != HAL_OK) {
        context->last_hal_error =
            HAL_I2C_GetError(context->handle);

        context->transfer_active = false;
        context->phase = STM32_I2C1_PHASE_IDLE;
    }

    return map_hal_status(status);
}

/*
 * The current I2cBusManager treats cancel() as completed when
 * this function returns. Therefore HAL_I2C_Master_Abort_IT()
 * must not be used here because it completes asynchronously.
 */
static Stm32AsyncHalStatus reset_peripheral(
    Stm32I2c1Hal *context)
{
    if (!context ||
        !context->handle ||
        context->handle->Instance != I2C1) {
        return STM32_ASYNC_HAL_ERROR;
    }

    uint32_t primask = critical_enter();

    /*
     * Remove a completion that may have arrived just before
     * timeout/cancellation.
     */
    clear_transfer_state(context);

    HAL_StatusTypeDef status =
        HAL_I2C_DeInit(context->handle);

    if (status == HAL_OK)
        status = HAL_I2C_Init(context->handle);

    if (status == HAL_OK) {
        status = HAL_I2CEx_ConfigAnalogFilter(
            context->handle,
            I2C_ANALOGFILTER_ENABLE);
    }

    if (status == HAL_OK) {
        status = HAL_I2CEx_ConfigDigitalFilter(
            context->handle,
            0u);
    }

    HAL_NVIC_ClearPendingIRQ(I2C1_EV_IRQn);
    HAL_NVIC_ClearPendingIRQ(I2C1_ER_IRQn);

    context->last_hal_error =
        HAL_I2C_GetError(context->handle);

    critical_exit(primask);

    return map_hal_status(status);
}

static Stm32AsyncHalStatus cancel(void *hal_context)
{
    return reset_peripheral(
        (Stm32I2c1Hal *)hal_context);
}

static Stm32AsyncHalStatus recover(void *hal_context)
{
    return reset_peripheral(
        (Stm32I2c1Hal *)hal_context);
}

static const Stm32I2cHalOps s_ops = {
    .start = start,
    .cancel = cancel,
    .recover = recover
};

bool stm32_i2c1_hal_init(
    Stm32I2c1Hal *context,
    I2C_HandleTypeDef *handle,
    Stm32I2cAdapter *adapter)
{
    if (!context ||
        !handle ||
        !adapter ||
        handle->Instance != I2C1 ||
        HAL_I2C_GetState(handle) !=
            HAL_I2C_STATE_READY ||
        (s_i2c1 && s_i2c1 != context)) {
        return false;
    }

    memset(context, 0, sizeof(*context));

    context->handle = handle;
    context->adapter = adapter;
    context->phase = STM32_I2C1_PHASE_IDLE;
    context->completion_result =
        PORT_STATUS_HARDWARE_ERROR;
    context->last_hal_error =
        HAL_I2C_ERROR_NONE;

    s_i2c1 = context;
    return true;
}

const Stm32I2cHalOps *stm32_i2c1_hal_ops(void)
{
    return &s_ops;
}

void stm32_i2c1_hal_poll(
    Stm32I2c1Hal *context)
{
    if (!context || !context->adapter)
        return;

    bool deliver = false;
    PortStatus result =
        PORT_STATUS_HARDWARE_ERROR;

    uint32_t primask = critical_enter();

    if (context->completion_pending) {
        result = context->completion_result;
        context->completion_pending = false;
        deliver = true;
    }

    critical_exit(primask);

    if (deliver) {
        i2c_port_stm32_on_complete(
            context->adapter,
            result);
    }
}

uint32_t stm32_i2c1_hal_last_error(
    const Stm32I2c1Hal *context)
{
    return context
        ? context->last_hal_error
        : HAL_I2C_ERROR_INVALID_PARAM;
}

/* Called by HAL_I2C_EV_IRQHandler(). */
void HAL_I2C_MasterTxCpltCallback(
    I2C_HandleTypeDef *handle)
{
    Stm32I2c1Hal *context = s_i2c1;

    if (!context ||
        handle != context->handle ||
        !context->transfer_active) {
        return;
    }

    if (context->phase ==
        STM32_I2C1_PHASE_TX_THEN_RX_TX) {
        context->phase =
            STM32_I2C1_PHASE_TX_THEN_RX_RX;

        HAL_StatusTypeDef status =
            HAL_I2C_Master_Seq_Receive_IT(
                context->handle,
                context->device_address,
                context->rx_buffer,
                context->rx_length,
                I2C_LAST_FRAME);

        if (status != HAL_OK) {
            context->last_hal_error =
                HAL_I2C_GetError(context->handle);

            latch_completion(
                context,
                PORT_STATUS_HARDWARE_ERROR);
        }

        return;
    }

    if (context->phase ==
        STM32_I2C1_PHASE_TX_ONLY) {
        latch_completion(context, PORT_OK);
    }
}

/* Called by HAL_I2C_EV_IRQHandler(). */
void HAL_I2C_MasterRxCpltCallback(
    I2C_HandleTypeDef *handle)
{
    Stm32I2c1Hal *context = s_i2c1;

    if (!context ||
        handle != context->handle ||
        !context->transfer_active) {
        return;
    }

    if (context->phase ==
            STM32_I2C1_PHASE_RX_ONLY ||
        context->phase ==
            STM32_I2C1_PHASE_TX_THEN_RX_RX) {
        latch_completion(context, PORT_OK);
    }
}

/* Called by HAL_I2C_ER_IRQHandler(). */
void HAL_I2C_ErrorCallback(
    I2C_HandleTypeDef *handle)
{
    Stm32I2c1Hal *context = s_i2c1;

    if (!context ||
        handle != context->handle ||
        !context->transfer_active) {
        return;
    }

    uint32_t error = HAL_I2C_GetError(handle);
    context->last_hal_error = error;

    PortStatus result =
        PORT_STATUS_HARDWARE_ERROR;

    if ((error & HAL_I2C_ERROR_TIMEOUT) != 0u) {
        result = PORT_STATUS_TIMEOUT;
    } else if ((error & HAL_I2C_ERROR_AF) != 0u) {
        /* Address or data byte was not acknowledged. */
        result = PORT_STATUS_UNAVAILABLE;
    }

    latch_completion(context, result);
}