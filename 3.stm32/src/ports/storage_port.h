#ifndef SWFPM_STORAGE_PORT_H
#define SWFPM_STORAGE_PORT_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Identifies one asynchronous storage operation.
 *
 * The token is supplied by the caller and returned unchanged in the terminal
 * completion so that the completion can be matched to its originating request.
 */
typedef struct {
    uint32_t operation_id; /**< Unique operation identifier assigned by the caller. */
    uint32_t
        correlation_id; /**< Identifier used to trace the request across components. */
    uint32_t
        owner_generation; /**< Owner generation used to reject obsolete operations. */
} StorageOperationToken;

/**
 * @brief Reports whether an asynchronous storage request was accepted.
 *
 * Only STORAGE_IO_SUBMIT_ACCEPTED transfers ownership of the operation to the
 * storage implementation and requires a later terminal completion.
 */
typedef enum {
    STORAGE_IO_SUBMIT_ACCEPTED =
        0,                  /**< Request accepted; a terminal completion will follow. */
    STORAGE_IO_SUBMIT_BUSY, /**< Port is already processing another operation. */
    STORAGE_IO_SUBMIT_INVALID_PARAM, /**< One or more request parameters are invalid. */
    STORAGE_IO_SUBMIT_OUT_OF_RANGE, /**< Requested address range exceeds storage capacity. */
    STORAGE_IO_SUBMIT_NOT_READY, /**< Storage device or underlying transport is not ready. */
    STORAGE_IO_SUBMIT_NO_CAPACITY /**< Required internal queue or resource is unavailable. */
} StorageIoSubmitResult;

/**
 * @brief Describes the terminal result of an accepted storage operation.
 */
typedef enum {
    STORAGE_IO_RESULT_OK = 0,    /**< Entire requested transfer completed successfully. */
    STORAGE_IO_RESULT_TIMEOUT,   /**< Operation did not finish before its deadline. */
    STORAGE_IO_RESULT_BUS_ERROR, /**< Underlying communication bus reported an error. */
    STORAGE_IO_RESULT_CANCELLED, /**< Operation was cancelled before normal completion. */
    STORAGE_IO_RESULT_STALE, /**< Operation belongs to an obsolete owner generation. */
    STORAGE_IO_RESULT_SHORT_TRANSFER, /**< Fewer bytes were transferred than requested. */
    STORAGE_IO_RESULT_INTERNAL_ERROR /**< Storage implementation encountered an internal fault. */
} StorageIoResult;

/**
 * @brief Terminal completion data for one accepted storage operation.
 */
typedef struct {
    StorageOperationToken token; /**< Caller token copied from the submitted operation. */
    StorageIoResult result;      /**< Final outcome of the operation. */
    uint16_t requested_length;   /**< Total number of bytes requested by the caller. */
    uint16_t transferred_length; /**< Number of bytes actually transferred. */
    uint32_t
        last_transaction_id; /**< Identifier of the last underlying bus transaction. */
    uint32_t
        client_generation; /**< Client generation recorded when completion was formed. */
    uint32_t bus_generation; /**< Bus generation associated with the final transaction. */
} StorageIoCompletion;

/**
 * @brief Receives the terminal completion of an asynchronous storage operation.
 *
 * The completion object is owned by the implementation and is valid only for
 * the duration of the callback unless the implementation documents otherwise.
 *
 * @param context Caller-provided callback context.
 * @param completion Terminal operation result; must not be NULL.
 */
typedef void (*StorageIoCompletionFn)(void* context,
                                      const StorageIoCompletion* completion);

/**
 * @brief Instance-owned asynchronous storage interface.
 *
 * Request buffers remain caller-owned and must stay valid until the matching
 * terminal completion is delivered. An implementation must emit exactly one
 * terminal completion for every accepted operation. Rejected submissions do
 * not produce a completion.
 */
typedef struct {
    void* context; /**< Implementation instance passed to every port function. */

    /**
     * @brief Binds the terminal-completion callback to the storage instance.
     *
     * The callback and its context must remain valid while the port can deliver
     * completions. Rebinding behavior is implementation-defined.
     *
     * @param context Storage implementation instance.
     * @param completion_fn Function invoked for terminal completions.
     * @param completion_context Context passed to completion_fn.
     *
     * @return true if the callback was bound successfully; otherwise false.
     */
    bool (*bind_completion)(void* context,
                            StorageIoCompletionFn completion_fn,
                            void* completion_context);

    /**
     * @brief Starts an asynchronous storage read.
     *
     * The destination buffer remains caller-owned and must stay valid until
     * terminal completion.
     *
     * @param context Storage implementation instance.
     * @param offset Byte offset of the first storage location to read.
     * @param buffer Caller-owned destination buffer.
     * @param size Number of bytes to read.
     * @param token Operation identity token.
     * @param deadline_us Absolute operation deadline in monotonic microseconds.
     *
     * @return Submission result.
     */
    StorageIoSubmitResult (*read_async)(void* context,
                                        uint32_t offset,
                                        uint8_t* buffer,
                                        uint16_t size,
                                        StorageOperationToken token,
                                        uint64_t deadline_us);

    /**
     * @brief Starts an asynchronous storage write.
     *
     * The source data remains caller-owned and must stay valid until terminal
     * completion.
     *
     * @param context Storage implementation instance.
     * @param offset Byte offset of the first storage location to write.
     * @param data Caller-owned source buffer.
     * @param size Number of bytes to write.
     * @param token Operation identity token.
     * @param deadline_us Absolute operation deadline in monotonic microseconds.
     *
     * @return Submission result.
     */
    StorageIoSubmitResult (*write_async)(void* context,
                                         uint32_t offset,
                                         const uint8_t* data,
                                         uint16_t size,
                                         StorageOperationToken token,
                                         uint64_t deadline_us);

    /**
     * @brief Invalidates operations from generations older than new_generation.
     *
     * An accepted in-flight operation invalidated by this call must still
     * receive exactly one terminal completion, normally with CANCELLED or STALE.
     *
     * @param context Storage implementation instance.
     * @param new_generation New active owner generation.
     */
    void (*cancel_generation)(void* context, uint32_t new_generation);

    /**
     * @brief Checks whether the storage instance has an operation in progress.
     *
     * @param context Storage implementation instance.
     *
     * @return true if an operation is active; otherwise false.
     */
    bool (*is_busy)(const void* context);
} StoragePort;

/**
 * @brief Validates that a storage port exposes the complete required contract.
 *
 * This function checks only pointer presence. It does not probe the storage
 * device or verify that the implementation is initialized and ready for I/O.
 *
 * @param port Storage port instance to validate.
 *
 * @return true if the port and all required members are non-NULL; otherwise false.
 */
static inline bool storage_port_is_valid(const StoragePort* port) {
    return port && port->context && port->bind_completion && port->read_async
           && port->write_async && port->cancel_generation && port->is_busy;
}

#endif
