#ifndef SWFPM_PORT_STATUS_H
#define SWFPM_PORT_STATUS_H

/**
 * @brief Common result status for platform ports.
 *
 * Returned by hardware APIs such as I2C, SPI, UART, ADC, and GPIO,
 * allowing upper layers to handle errors without depending directly
 * on the STM32 HAL.
 */
typedef enum {
    PORT_OK,                    /**< Operation completed successfully. */
    PORT_STATUS_TIMEOUT,        /**< Operation timed out. */
    PORT_STATUS_UNAVAILABLE,    /**< Port is unavailable or not initialized. */
    PORT_STATUS_INVALID_PARAM,  /**< Invalid input parameter. */
    PORT_STATUS_HARDWARE_ERROR, /**< Hardware or peripheral failure. */
    PORT_STATUS_BUSY,           /**< Port is busy with another operation. */
} PortStatus;

#endif
