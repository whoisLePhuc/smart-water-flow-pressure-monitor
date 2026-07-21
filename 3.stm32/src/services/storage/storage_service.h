#ifndef SWFPM_STORAGE_SERVICE_H
#define SWFPM_STORAGE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "ports/storage_port.h"
#include "protocols/storage/storage_record.h"

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
    STORAGE_COMMIT_OK,
    STORAGE_COMMIT_REJECTED,
    STORAGE_COMMIT_ENCODE_ERROR,
    STORAGE_COMMIT_IO_ERROR,
    STORAGE_COMMIT_VERIFY_ERROR,
    STORAGE_COMMIT_SEQUENCE_CONFLICT,
    STORAGE_COMMIT_CANCELLED_GENERATION,
    STORAGE_COMMIT_INTERNAL_ERROR
} StorageCommitStatus;

typedef struct {
    uint64_t request_id;
    uint64_t candidate_version;
    uint8_t record_type;
    uint8_t selected_slot;
    uint32_t record_sequence;
    StorageCommitStatus status;
} StorageCompletionPayload;

#define STORAGE_MAX_SLOT_SIZE SLOT_CALIBRATION_SIZE
#define STORAGE_BODY_CHUNK_BYTES 32u

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

typedef struct {
    StorageServiceState state;
    uint8_t record_type;
    uint32_t sequence;
    uint64_t candidate_version;
    uint64_t request_id;
    uint16_t encoded_length;
    uint8_t slot_buffer[STORAGE_MAX_SLOT_SIZE];
    uint8_t readback[STORAGE_MAX_SLOT_SIZE];
    uint8_t scan_a[STORAGE_MAX_SLOT_SIZE];
    uint8_t scan_b[STORAGE_MAX_SLOT_SIZE];
    uint8_t target_slot;
    uint16_t target_address;
    uint16_t slot_size;
    uint16_t write_offset;
    uint8_t io_byte;

    bool io_pending;
    bool io_completed;
    StorageOperationToken io_token;
    StorageIoCompletion io_completion;
    StorageServiceState io_success_state;
    uint16_t io_advance_bytes;

    bool pending;
    uint8_t pending_buffer[STORAGE_MAX_SLOT_SIZE];
    uint16_t pending_length;
    uint64_t pending_version;
    uint32_t pending_sequence;
    uint8_t pending_type;
} StorageServiceContext;

typedef struct {
    uint64_t forward_volume_ul;
    uint64_t reverse_volume_ul;
    uint64_t forward_remainder;
    uint64_t reverse_remainder;
    uint64_t state_version;
    uint64_t last_flow_sequence;
    uint32_t last_source_generation;
} StorageRestoredVolume;

typedef struct StorageServiceImpl {
    StoragePort port;
    StorageServiceContext context;
    uint32_t generation;
    uint32_t next_operation_id;
    uint32_t next_correlation_id;
    uint32_t io_timeout_us;
    uint64_t request_count;

    StorageCompletionPayload last_completion;
    bool completion_ready;
    StorageRestoreStatus restore_status;
    StorageRestoredVolume restored_volume;
    bool restore_ready;
    uint32_t stale_completion_count;
} StorageService;

StorageStatus StorageService_Init(StorageService *self,
                                  const StoragePort *port,
                                  uint32_t io_timeout_us);

StorageStatus StorageService_SubmitCheckpoint(
    StorageService *self,
    uint8_t record_type,
    uint32_t sequence,
    const uint8_t *encoded_buffer,
    uint16_t encoded_length,
    uint64_t candidate_version);

/* Advances bounded cooperative work and submits at most one I/O operation. */
void StorageService_Tick(StorageService *self, uint64_t now_us);

void StorageService_OnIoCompletion(
    StorageService *self,
    const StorageIoCompletion *completion);

bool StorageService_TakeCompletion(StorageService *self,
                                   StorageCompletionPayload *completion_out);

StorageStatus StorageService_StartRestoreVolume(StorageService *self);

bool StorageService_TakeRestoredVolume(
    StorageService *self,
    StorageRestoreStatus *status_out,
    StorageRestoredVolume *volume_out);

#endif /* SWFPM_STORAGE_SERVICE_H */
