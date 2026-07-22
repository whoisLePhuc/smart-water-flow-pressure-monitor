#ifndef SWFPM_I2C_PORT_H
#define SWFPM_I2C_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "ports/port_status.h"

/**
 * @brief Describes one asynchronous I2C transaction submitted to a platform.
 *
 * The request object and referenced buffers remain caller-owned. The platform
 * must retain or copy any information that it needs after submit() returns.
 * The transmit buffer must remain readable and the receive buffer must remain
 * writable until the transaction reaches terminal completion or is cancelled.
 */
typedef struct {
    /** Unique identifier used to match submission, cancellation and completion. */
    uint32_t transaction_id;

    /** Higher-level identifier used to trace the transaction across layers. */
    uint32_t correlation_id;

    /** Generation of the client that created this request. */
    uint32_t client_generation;

    /** Bus generation under which this transaction is valid. */
    uint32_t bus_generation;

    /** Target device address in the address format required by the port contract. */
    uint8_t slave_address;

    /** Caller-owned transmit buffer; may be NULL when tx_length is zero. */
    const uint8_t* tx;

    /** Number of bytes to transmit from tx. */
    uint16_t tx_length;

    /** Caller-owned receive buffer; may be NULL when rx_length is zero. */
    uint8_t* rx;

    /** Maximum number of bytes to store in rx. */
    uint16_t rx_length;

    /** Absolute transaction deadline in the system monotonic time domain. */
    uint64_t deadline_us;
} I2cPortRequest;

/**
 * @brief Platform-independent interface for asynchronous I2C bus operations.
 *
 * The interface instance does not own context. Its owner must keep context and
 * the function table valid for as long as callers can access this port.
 */
typedef struct {
    /** Platform-specific state passed unchanged to every port operation. */
    void* context;

    /**
     * @brief Submits an asynchronous I2C transaction.
     *
     * Acceptance means that the platform has taken responsibility for the
     * transaction. The request and its buffers remain caller-owned according
     * to the lifetime rules documented by I2cPortRequest.
     *
     * @param context Platform-specific port context.
     * @param request Transaction description to submit.
     *
     * @return Submission status reported by the platform.
     */
    PortStatus (*submit)(void* context, const I2cPortRequest* request);

    /**
     * @brief Requests cancellation of a previously submitted transaction.
     *
     * bus_generation prevents a cancellation from affecting a transaction
     * that reuses the same identifier after bus recovery.
     *
     * @param context Platform-specific port context.
     * @param transaction_id Identifier of the transaction to cancel.
     * @param bus_generation Bus generation in which the transaction was issued.
     *
     * @return Cancellation request status reported by the platform.
     */
    PortStatus (*cancel)(void* context, uint32_t transaction_id, uint32_t bus_generation);

    /**
     * @brief Recovers or reinitializes the I2C peripheral for a new generation.
     *
     * Advancing the generation invalidates outstanding work associated with an
     * older bus state and prevents stale completion from being accepted as new.
     *
     * @param context Platform-specific port context.
     * @param new_bus_generation Generation assigned to the recovered bus.
     *
     * @return Recovery status reported by the platform.
     */
    PortStatus (*recover)(void* context, uint32_t new_bus_generation);
} I2cPort;

/**
 * @brief Checks whether an I2C port provides the minimum callable interface.
 *
 * cancel() and recover() are optional capabilities and therefore are not
 * required by this basic validity check.
 *
 * @param port Port instance to validate.
 *
 * @return true when port is non-NULL and provides submit(); otherwise false.
 */
static inline bool i2c_port_is_valid(const I2cPort* port) {
    return port && port->submit;
}

#endif
