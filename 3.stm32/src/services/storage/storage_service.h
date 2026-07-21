#ifndef SWFPM_STORAGE_SERVICE_H
#define SWFPM_STORAGE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "ports/storage_port.h"
#include "protocols/storage/storage_record.h"

/** @brief Status codes returned by storage service operations. */
typedef enum {
    STORAGE_OK,
    STORAGE_BUSY,
    STORAGE_REJECTED,
    STORAGE_ENCODE_ERROR,
    STORAGE_IO_ERROR,
    STORAGE_VERIFY_ERROR,
    STORAGE_SEQUENCE_CONFLICT,
    STORAGE_CANCELLED_GENERATION,
    STORAGE_INTERNAL_ERROR
} StorageStatus;

/** @brief Status codes for volume restore operations. */
typedef enum {
    STORAGE_RESTORE_OK,
    STORAGE_RESTORE_EMPTY,
    STORAGE_RESTORE_CORRUPT,
    STORAGE_RESTORE_UNSUPPORTED_SCHEMA,
    STORAGE_RESTORE_INCOMPATIBLE,
    STORAGE_RESTORE_SEQUENCE_CONFLICT,
    STORAGE_RESTORE_IO_ERROR,
    STORAGE_RESTORE_INTERNAL_ERROR
} StorageRestoreStatus;

/** @brief Status codes for checkpoint commit operations. */
typedef enum {
    STORAGE_COMMIT_OK,
    STORAGE_COMMIT_REJECTED,
    STORAGE_COMMIT_ENCODE_ERROR,
    STORAGE_COMMIT_IO_ERROR,
    STORAGE_COMMIT_VERIFY_ERROR,
    STORAGE_COMMIT_SEQUENCE_CONFLICT,
    STORAGE_COMMIT_CANCELLED_GENERATION,
    STORAGE_COMMIT_INTERNAL_ERROR
} StorageCommitStatus;

/** @brief Payload delivered via TakeCompletion after a commit finishes. */
typedef struct {
    uint64_t request_id;           /**< @brief Unique request identifier. */
    uint64_t candidate_version;    /**< @brief Version of the checkpoint candidate. */
    uint8_t record_type;           /**< @brief Type of record committed. */
    uint8_t selected_slot;         /**< @brief A/B slot selected for storage. */
    uint32_t record_sequence;      /**< @brief Sequence number of committed record. */
    StorageCommitStatus status;    /**< @brief Final commit outcome. */
} StorageCompletionPayload;

#define STORAGE_MAX_SLOT_SIZE SLOT_CALIBRATION_SIZE
#define STORAGE_BODY_CHUNK_BYTES 32u

/** @brief Finite-state-machine states for storage commit and restore operations. */
typedef enum {
    STORAGE_STATE_IDLE,
    STORAGE_STATE_SCAN_A,
    STORAGE_STATE_SCAN_B,
    STORAGE_STATE_PREPARE_TARGET,
    STORAGE_STATE_INVALIDATE,
    STORAGE_STATE_VERIFY_INVALIDATE,
    STORAGE_STATE_CHECK_INVALIDATE,
    STORAGE_STATE_WRITE_BODY,
    STORAGE_STATE_READBACK_BODY,
    STORAGE_STATE_VERIFY_BODY,
    STORAGE_STATE_COMMIT,
    STORAGE_STATE_VERIFY_COMMIT,
    STORAGE_STATE_CHECK_COMMIT,
    STORAGE_STATE_COMPLETE,
    STORAGE_STATE_FAILED,
    STORAGE_STATE_RESTORE_SCAN_A,
    STORAGE_STATE_RESTORE_SCAN_B,
    STORAGE_STATE_RESTORE_DECODE,
    STORAGE_STATE_RESTORE_COMPLETE,
    STORAGE_STATE_RESTORE_FAILED
} StorageServiceState;

/** @brief Runtime context for the storage commit and restore state machine. */
typedef struct {
    StorageServiceState state;         /**< @brief Current FSM state. */
    uint8_t record_type;               /**< @brief Type of record being committed. */
    uint32_t sequence;                 /**< @brief Record sequence number. */
    uint64_t candidate_version;        /**< @brief Version of the candidate being committed. */
    uint64_t request_id;               /**< @brief Request identifier from SubmitCheckpoint. */
    uint16_t encoded_length;           /**< @brief Size of the encoded record in bytes. */
    uint8_t slot_buffer[STORAGE_MAX_SLOT_SIZE];  /**< @brief Working buffer for the active candidate. */
    uint8_t readback[STORAGE_MAX_SLOT_SIZE];     /**< @brief Readback buffer for verify-after-write. */
    uint8_t scan_a[STORAGE_MAX_SLOT_SIZE];       /**< @brief Content read from slot A. */
    uint8_t scan_b[STORAGE_MAX_SLOT_SIZE];       /**< @brief Content read from slot B. */
    uint8_t target_slot;               /**< @brief Selected slot index (0 or 1). */
    uint16_t target_address;           /**< @brief Byte address of the selected slot. */
    uint16_t slot_size;                /**< @brief Total slot size in bytes. */
    uint16_t write_offset;             /**< @brief Current write position within the slot. */
    uint8_t io_byte;                   /**< @brief Single-byte buffer for commit/invalidate flags. */
    bool io_pending;                   /**< @brief Async I/O operation in flight. */
    bool io_completed;                 /**< @brief Async I/O operation finished. */
    StorageOperationToken io_token;            /**< @brief Token matching async I/O to its completion. */
    StorageIoCompletion io_completion;         /**< @brief Storage driver completion record. */
    StorageServiceState io_success_state;      /**< @brief FSM state to enter on I/O success. */
    uint16_t io_advance_bytes;         /**< @brief Bytes to advance write_offset on success. */
    bool pending;                      /**< @brief Checkpoint queued while state machine is busy. */
    uint8_t pending_buffer[STORAGE_MAX_SLOT_SIZE];  /**< @brief Buffer for the queued checkpoint. */
    uint16_t pending_length;           /**< @brief Length of the queued checkpoint. */
    uint64_t pending_version;          /**< @brief Version of the queued checkpoint. */
    uint32_t pending_sequence;         /**< @brief Sequence number of the queued checkpoint. */
    uint8_t pending_type;              /**< @brief Record type of the queued checkpoint. */
} StorageServiceContext;

/** @brief Restored volume data read from persistent storage during boot. */
typedef struct {
    uint64_t forward_volume_ul;       /**< @brief Forward total volume in microlitres. */
    uint64_t reverse_volume_ul;       /**< @brief Reverse total volume in microlitres. */
    uint64_t forward_remainder;       /**< @brief Forward remainder carried over. */
    uint64_t reverse_remainder;       /**< @brief Reverse remainder carried over. */
    uint64_t state_version;           /**< @brief State version at time of commit. */
    uint64_t last_flow_sequence;      /**< @brief Last flow sequence at time of commit. */
    uint32_t last_source_generation;  /**< @brief Last source generation at time of commit. */
} StorageRestoredVolume;

/** @brief Main storage service instance holding port binding and FSM state. */
typedef struct StorageServiceImpl {
    StoragePort port;                  /**< @brief Bound storage driver port. */
    StorageServiceContext context;     /**< @brief Commit and restore FSM context. */
    uint32_t generation;               /**< @brief Generation counter for stale completion detection. */
    uint32_t next_operation_id;        /**< @brief Next available operation identifier. */
    uint32_t next_correlation_id;      /**< @brief Next available correlation identifier. */
    uint32_t io_timeout_us;            /**< @brief I/O operation timeout in microseconds. */
    uint64_t request_count;            /**< @brief Monotonically increasing request counter. */
    StorageCompletionPayload last_completion;   /**< @brief Most recent completion payload. */
    bool completion_ready;             /**< @brief Completion ready to be consumed. */
    StorageRestoreStatus restore_status;        /**< @brief Status of the most recent restore. */
    StorageRestoredVolume restored_volume;      /**< @brief Restored volume data. */
    bool restore_ready;                /**< @brief Restored volume data ready to be consumed. */
    uint32_t stale_completion_count;   /**< @brief Count of discarded stale completions. */
} StorageService;

/** @brief Initialise the storage service and bind it to a storage port.
 *  @param self Storage service instance.
 *  @param port Storage driver port to bind.
 *  @param io_timeout_us I/O operation timeout in microseconds.
 *  @return STORAGE_OK on success, or an error status. */
StorageStatus StorageService_Init(StorageService* self,
                                  const StoragePort* port,
                                  uint32_t io_timeout_us);

/** @brief Submit a checkpoint record for asynchronous commit.
 *  @param self Storage service instance.
 *  @param record_type Type of record to commit.
 *  @param sequence Record sequence number.
 *  @param encoded_buffer Pointer to the encoded record data.
 *  @param encoded_length Size of the encoded record in bytes.
 *  @param candidate_version Version of the checkpoint candidate.
 *  @return STORAGE_OK if accepted, STORAGE_BUSY if queued, or an error status. */
StorageStatus StorageService_SubmitCheckpoint(StorageService* self,
                                              uint8_t record_type,
                                              uint32_t sequence,
                                              const uint8_t* encoded_buffer,
                                              uint16_t encoded_length,
                                              uint64_t candidate_version);

/** @brief Advances bounded cooperative work and submits at most one I/O operation.
 *  @param self Storage service instance.
 *  @param now_us Current time in microseconds. */
void StorageService_Tick(StorageService* self, uint64_t now_us);

/** @brief Handle an I/O completion from the storage driver.
 *  @param self Storage service instance.
 *  @param completion Completed I/O operation details. */
void StorageService_OnIoCompletion(StorageService* self,
                                   const StorageIoCompletion* completion);

/** @brief Take the most recent commit completion payload, if ready.
 *  @param self Storage service instance.
 *  @param completion_out Output parameter for the completion payload.
 *  @return true if a completion was available and copied. */
bool StorageService_TakeCompletion(StorageService* self,
                                   StorageCompletionPayload* completion_out);

/** @brief Start a restore operation to read volume data from persistent storage.
 *  @param self Storage service instance.
 *  @return STORAGE_OK if accepted, or an error status. */
StorageStatus StorageService_StartRestoreVolume(StorageService* self);

/** @brief Take the restored volume data, if ready.
 *  @param self Storage service instance.
 *  @param status_out Output parameter for the restore status.
 *  @param volume_out Output parameter for the restored volume data.
 *  @return true if restored data was available and copied. */
bool StorageService_TakeRestoredVolume(StorageService* self,
                                       StorageRestoreStatus* status_out,
                                       StorageRestoredVolume* volume_out);

#endif /* SWFPM_STORAGE_SERVICE_H */
