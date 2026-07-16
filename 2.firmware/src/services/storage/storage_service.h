#ifndef SWFPM_STORAGE_SERVICE_H
#define SWFPM_STORAGE_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "protocols/storage/storage_record.h"
#include "drivers/storage/fram_driver.h"

/* =================================================================
 * Storage status enums (T010)
 * ================================================================= */

typedef enum {
    STORAGE_OK,
    STORAGE_BUSY,                   /* in-flight commit in progress */
    STORAGE_REJECTED,               /* request rejected (bad type/version/generation) */
    STORAGE_ENCODE_ERROR,
    STORAGE_IO_ERROR,
    STORAGE_VERIFY_ERROR,
    STORAGE_SEQUENCE_CONFLICT,
    STORAGE_CANCELLED_GENERATION,
    STORAGE_INTERNAL_ERROR
} StorageStatus;

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

typedef enum {
    FRAM_OK,
    FRAM_SUBMITTED,                 /* async operation started */
    FRAM_PENDING,                   /* in-flight */
    FRAM_COMPLETED,                 /* terminal success */
    FRAM_FAILED,
    FRAM_TIMEOUT,
    FRAM_OUT_OF_RANGE,
    FRAM_INVALID_PARAM
} FramStatus;

/* =================================================================
 * Storage service types
 * ================================================================= */

/* Terminal commit outcome */
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

/* Storage commit completion event payload */
typedef struct {
    uint64_t            request_id;
    uint64_t            candidate_version;
    uint8_t             record_type;
    uint8_t             selected_slot;      /* 0 = A, 1 = B */
    uint32_t            record_sequence;
    StorageCommitStatus status;
} StorageCompletionPayload;

/* =================================================================
 * StorageService public API
 * ================================================================= */

/* Public, statically allocatable state. No heap and no hidden allocator are
 * required; ownership belongs to the composition root. */
#define STORAGE_MAX_SLOT_SIZE SLOT_CALIBRATION_SIZE

typedef enum {
    STORAGE_STATE_IDLE,
    STORAGE_STATE_ENCODE,
    STORAGE_STATE_INVALIDATE,
    STORAGE_STATE_VERIFY_INVALIDATE,
    STORAGE_STATE_WRITE_BODY,
    STORAGE_STATE_READBACK_BODY,
    STORAGE_STATE_VERIFY_BODY,
    STORAGE_STATE_COMMIT,
    STORAGE_STATE_VERIFY_COMMIT,
    STORAGE_STATE_COMPLETE,
    STORAGE_STATE_FAILED
} StorageServiceState;

typedef struct {
    StorageServiceState state;
    uint8_t record_type;
    uint32_t sequence;
    uint64_t candidate_version;
    uint16_t encoded_length;
    uint8_t slot_buffer[STORAGE_MAX_SLOT_SIZE];
    uint8_t readback[STORAGE_MAX_SLOT_SIZE];
    uint8_t target_slot;
    uint16_t target_address;
    uint16_t slot_size;
    uint16_t write_offset;
    bool pending;
    uint8_t pending_buffer[STORAGE_MAX_SLOT_SIZE];
    uint16_t pending_length;
    uint64_t pending_version;
    uint32_t pending_sequence;
    uint8_t pending_type;
} StorageServiceContext;

typedef struct StorageServiceImpl {
    FramDriver *fram;
    StorageServiceContext context;
    uint32_t generation;
    uint64_t request_count;
} StorageService;

/* Initialize storage service with F-RAM driver and I2C bus manager. */
StorageStatus StorageService_Init(StorageService *self, FramDriver *fram);

/* Submit a checkpoint candidate for async commit.
 * Returns STORAGE_OK if accepted, STORAGE_BUSY if in-flight (candidate queued latest-wins). */
StorageStatus StorageService_SubmitCheckpoint(
    StorageService *self,
    uint8_t         record_type,
    uint32_t        sequence,
    const uint8_t  *encoded_buffer,
    uint16_t        encoded_length,
    uint64_t        candidate_version);

/* Advance storage state machine (call from event loop). */
void StorageService_Tick(StorageService *self);

/* Restore volume record from F-RAM.
 * Returns restore status; if OK, populates output fields. */
StorageRestoreStatus StorageService_RestoreVolume(
    StorageService  *self,
    uint64_t        *forward_volume_ul,
    uint64_t        *reverse_volume_ul,
    uint64_t        *forward_remainder,
    uint64_t        *reverse_remainder,
    uint64_t        *state_version,
    uint64_t        *last_flow_sequence,
    uint32_t        *last_source_generation);

#endif /* SWFPM_STORAGE_SERVICE_H */
