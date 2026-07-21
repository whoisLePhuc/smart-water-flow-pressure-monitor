#include "services/storage/storage_service.h"

#include <string.h>

/** @brief Get the memory address of an A/B slot for a given record type.
 *  @param slot Slot index (0 = A, 1 = B).
 *  @param type Record type identifier.
 *  @return Byte address of the slot, or 0 if type is unknown. */
static uint16_t slot_addr_for(uint8_t slot, uint8_t type) {
    if (type == PERSIST_RECORD_VOLUME)
        return slot ? SLOT_VOLUME_B_ADDR : SLOT_VOLUME_A_ADDR;
    if (type == PERSIST_RECORD_CONFIG)
        return slot ? SLOT_CONFIG_B_ADDR : SLOT_CONFIG_A_ADDR;
    if (type == PERSIST_RECORD_CALIBRATION)
        return slot ? SLOT_CALIBRATION_B_ADDR : SLOT_CALIBRATION_A_ADDR;
    return 0u;
}

/** @brief Get the slot size for a given record type.
 *  @param type Record type identifier.
 *  @return Slot size in bytes, or 0 if type is unknown. */
static uint16_t slot_size_for(uint8_t type) {
    if (type == PERSIST_RECORD_VOLUME)
        return SLOT_VOLUME_SIZE;
    if (type == PERSIST_RECORD_CONFIG)
        return SLOT_CONFIG_SIZE;
    if (type == PERSIST_RECORD_CALIBRATION)
        return SLOT_CALIBRATION_SIZE;
    return 0u;
}

/** @brief Check if an FSM state is a terminal state.
 *  @param state FSM state to check.
 *  @return true if the state is IDLE, COMPLETE, or FAILED. */
static bool is_terminal(StorageServiceState state) {
    return state == STORAGE_STATE_IDLE || state == STORAGE_STATE_COMPLETE
           || state == STORAGE_STATE_FAILED;
}

/** @brief Check if an FSM state belongs to the restore sub-machine.
 *  @param state FSM state to check.
 *  @return true if the state is a restore state. */
static bool is_restore_state(StorageServiceState state) {
    return state == STORAGE_STATE_RESTORE_SCAN_A || state == STORAGE_STATE_RESTORE_SCAN_B
           || state == STORAGE_STATE_RESTORE_DECODE
           || state == STORAGE_STATE_RESTORE_COMPLETE
           || state == STORAGE_STATE_RESTORE_FAILED;
}

/** @brief Callback bridge from the storage port to OnIoCompletion.
 *  @param context StorageService instance pointer.
 *  @param completion Completed I/O operation details. */
static void storage_completion_sink(void* context,
                                    const StorageIoCompletion* completion) {
    StorageService_OnIoCompletion((StorageService*)context, completion);
}

/** @brief Generate the next unique operation token for async I/O.
 *  @param self Storage service instance.
 *  @return A new operation token with unique IDs and the current generation. */
static StorageOperationToken next_token(StorageService* self) {
    StorageOperationToken token = {.operation_id = self->next_operation_id++,
                                   .correlation_id = self->next_correlation_id++,
                                   .owner_generation = self->generation};
    if (token.operation_id == 0u) {
        token.operation_id = self->next_operation_id++;
    }
    if (token.correlation_id == 0u) {
        token.correlation_id = self->next_correlation_id++;
    }
    return token;
}

/** @brief Submit an asynchronous read operation to the storage port.
 *  @param self Storage service instance.
 *  @param offset Byte offset to read from.
 *  @param buffer Destination buffer for the read data.
 *  @param size Number of bytes to read.
 *  @param success_state FSM state to enter on success.
 *  @param advance_bytes Bytes to advance write_offset on success.
 *  @param now_us Current time in microseconds.
 *  @return true if the read was accepted by the port. */
static bool submit_read(StorageService* self,
                        uint32_t offset,
                        uint8_t* buffer,
                        uint16_t size,
                        StorageServiceState success_state,
                        uint16_t advance_bytes,
                        uint64_t now_us) {
    if (UINT64_MAX - now_us < self->io_timeout_us)
        return false;
    StorageServiceContext* context = &self->context;
    context->io_token = next_token(self);
    context->io_pending = true;
    context->io_completed = false;
    context->io_success_state = success_state;
    context->io_advance_bytes = advance_bytes;

    StorageIoSubmitResult result = self->port.read_async(self->port.context,
                                                         offset,
                                                         buffer,
                                                         size,
                                                         context->io_token,
                                                         now_us + self->io_timeout_us);
    if (result != STORAGE_IO_SUBMIT_ACCEPTED) {
        context->io_pending = false;
        context->io_completed = false;
        return false;
    }
    return true;
}

/** @brief Submit an asynchronous write operation to the storage port.
 *  @param self Storage service instance.
 *  @param offset Byte offset to write to.
 *  @param buffer Source buffer with data to write.
 *  @param size Number of bytes to write.
 *  @param success_state FSM state to enter on success.
 *  @param advance_bytes Bytes to advance write_offset on success.
 *  @param now_us Current time in microseconds.
 *  @return true if the write was accepted by the port. */
static bool submit_write(StorageService* self,
                         uint32_t offset,
                         const uint8_t* buffer,
                         uint16_t size,
                         StorageServiceState success_state,
                         uint16_t advance_bytes,
                         uint64_t now_us) {
    if (UINT64_MAX - now_us < self->io_timeout_us)
        return false;
    StorageServiceContext* context = &self->context;
    context->io_token = next_token(self);
    context->io_pending = true;
    context->io_completed = false;
    context->io_success_state = success_state;
    context->io_advance_bytes = advance_bytes;

    StorageIoSubmitResult result = self->port.write_async(self->port.context,
                                                          offset,
                                                          buffer,
                                                          size,
                                                          context->io_token,
                                                          now_us + self->io_timeout_us);
    if (result != STORAGE_IO_SUBMIT_ACCEPTED) {
        context->io_pending = false;
        context->io_completed = false;
        return false;
    }
    return true;
}

/** @brief Finalize a commit with the given status and populate the completion payload.
 *  @param self Storage service instance.
 *  @param status Commit status to record. */
static void finish_commit(StorageService* self, StorageCommitStatus status) {
    StorageServiceContext* context = &self->context;
    self->last_completion =
        (StorageCompletionPayload){.request_id = context->request_id,
                                   .candidate_version = context->candidate_version,
                                   .record_type = context->record_type,
                                   .selected_slot = context->target_slot,
                                   .record_sequence = context->sequence,
                                   .status = status};
    self->completion_ready = true;
    context->state =
        status == STORAGE_COMMIT_OK ? STORAGE_STATE_COMPLETE : STORAGE_STATE_FAILED;
}

/** @brief Handle an I/O failure and transition to the appropriate error state.
 *  @param self Storage service instance.
 *  @param result I/O result that caused the failure. */
static void fail_io(StorageService* self, StorageIoResult result) {
    if (is_restore_state(self->context.state)) {
        self->restore_status = STORAGE_RESTORE_IO_ERROR;
        self->restore_ready = true;
        self->context.state = STORAGE_STATE_RESTORE_FAILED;
        return;
    }
    finish_commit(self,
                  result == STORAGE_IO_RESULT_CANCELLED
                      ? STORAGE_COMMIT_CANCELLED_GENERATION
                      : STORAGE_COMMIT_IO_ERROR);
}

/** @brief Consume a completed I/O operation and advance the FSM state.
 *  @param self Storage service instance.
 *  @return true if a completion was consumed. */
static bool consume_io_completion(StorageService* self) {
    StorageServiceContext* context = &self->context;
    if (!context->io_pending || !context->io_completed)
        return false;

    StorageIoCompletion completion = context->io_completion;
    context->io_pending = false;
    context->io_completed = false;
    if (completion.result != STORAGE_IO_RESULT_OK
        || completion.transferred_length != completion.requested_length) {
        fail_io(self,
                completion.result == STORAGE_IO_RESULT_OK
                    ? STORAGE_IO_RESULT_SHORT_TRANSFER
                    : completion.result);
        return true;
    }
    context->write_offset = (uint16_t)(context->write_offset + context->io_advance_bytes);
    context->state = context->io_success_state;
    return true;
}

/** @brief Prepare a new checkpoint candidate in the FSM context and begin scanning.
 *  @param self Storage service instance.
 *  @param record_type Type of record.
 *  @param sequence Record sequence number.
 *  @param encoded_buffer Encoded record data.
 *  @param encoded_length Size of encoded data in bytes.
 *  @param candidate_version Version number.
 *  @return true if the candidate was accepted. */
static bool prepare_active_candidate(StorageService* self,
                                      uint8_t record_type,
                                      uint32_t sequence,
                                      const uint8_t* encoded_buffer,
                                      uint16_t encoded_length,
                                      uint64_t candidate_version) {
    uint16_t slot_size = slot_size_for(record_type);
    if (!slot_size || slot_size > STORAGE_MAX_SLOT_SIZE
        || encoded_length < PERSIST_COMMON_HEADER_SIZE || encoded_length > slot_size)
        return false;

    StorageServiceContext* context = &self->context;
    context->record_type = record_type;
    context->sequence = sequence;
    context->candidate_version = candidate_version;
    context->request_id = ++self->request_count;
    context->encoded_length = encoded_length;
    context->slot_size = slot_size;
    context->write_offset = 0u;
    context->pending = false;
    memset(context->slot_buffer, 0, sizeof(context->slot_buffer));
    memcpy(context->slot_buffer, encoded_buffer, encoded_length);
    memset(context->readback, 0, sizeof(context->readback));
    memset(context->scan_a, 0, sizeof(context->scan_a));
    memset(context->scan_b, 0, sizeof(context->scan_b));
    context->state = STORAGE_STATE_SCAN_A;
    return true;
}

/** @brief Promote the queued pending checkpoint to an active candidate.
 *  @param self Storage service instance. */
static void promote_pending(StorageService* self) {
    StorageServiceContext* context = &self->context;
    uint8_t type = context->pending_type;
    uint32_t sequence = context->pending_sequence;
    uint64_t version = context->pending_version;
    uint16_t length = context->pending_length;
    uint8_t candidate[STORAGE_MAX_SLOT_SIZE];
    memcpy(candidate, context->pending_buffer, sizeof(candidate));
    context->pending = false;
    if (!prepare_active_candidate(self, type, sequence, candidate, length, version))
        finish_commit(self, STORAGE_COMMIT_REJECTED);
}

/** @brief Initialise the storage service and bind it to a storage port.
 *  @param self Storage service instance.
 *  @param port Storage driver port to bind.
 *  @param io_timeout_us I/O operation timeout in microseconds.
 *  @return STORAGE_OK on success, or an error status. */
StorageStatus StorageService_Init(StorageService* self,
                                  const StoragePort* port,
                                  uint32_t io_timeout_us) {
    if (!self || !storage_port_is_valid(port) || io_timeout_us == 0u)
        return STORAGE_REJECTED;
    memset(self, 0, sizeof(*self));
    self->port = *port;
    self->context.state = STORAGE_STATE_IDLE;
    self->generation = 1u;
    self->next_operation_id = 1u;
    self->next_correlation_id = 1u;
    self->io_timeout_us = io_timeout_us;
    if (!self->port.bind_completion(self->port.context, storage_completion_sink, self)) {
        memset(self, 0, sizeof(*self));
        return STORAGE_REJECTED;
    }
    return STORAGE_OK;
}

/** @brief Submit a checkpoint record for asynchronous commit.
 *  @param self Storage service instance.
 *  @param record_type Type of record to commit.
 *  @param sequence Record sequence number.
 *  @param encoded_buffer Encoded record data.
 *  @param encoded_length Size of encoded data in bytes.
 *  @param candidate_version Version number.
 *  @return STORAGE_OK if started, STORAGE_BUSY if queued, or an error status. */
StorageStatus StorageService_SubmitCheckpoint(StorageService* self,
                                              uint8_t record_type,
                                              uint32_t sequence,
                                              const uint8_t* encoded_buffer,
                                              uint16_t encoded_length,
                                              uint64_t candidate_version) {
    if (!self || !encoded_buffer || encoded_length == 0u
        || is_restore_state(self->context.state))
        return STORAGE_REJECTED;

    StorageServiceContext* context = &self->context;
    if (is_terminal(context->state)) {
        if (self->completion_ready)
            return STORAGE_BUSY;
        if (!prepare_active_candidate(self,
                                      record_type,
                                      sequence,
                                      encoded_buffer,
                                      encoded_length,
                                      candidate_version))
            return STORAGE_REJECTED;
        return STORAGE_OK;
    }

    uint16_t slot_size = slot_size_for(record_type);
    if (!slot_size || encoded_length < PERSIST_COMMON_HEADER_SIZE
        || encoded_length > slot_size)
        return STORAGE_REJECTED;
    context->pending = true;
    context->pending_type = record_type;
    context->pending_sequence = sequence;
    context->pending_version = candidate_version;
    context->pending_length = encoded_length;
    memset(context->pending_buffer, 0, sizeof(context->pending_buffer));
    memcpy(context->pending_buffer, encoded_buffer, encoded_length);
    return STORAGE_BUSY;
}

/** @brief Process an I/O completion notification from the storage driver.
 *  @param self Storage service instance.
 *  @param completion Completed I/O operation details. */
void StorageService_OnIoCompletion(StorageService* self,
                                   const StorageIoCompletion* completion) {
    if (!self || !completion)
        return;
    StorageServiceContext* context = &self->context;
    if (!context->io_pending || context->io_completed
        || completion->token.operation_id != context->io_token.operation_id
        || completion->token.correlation_id != context->io_token.correlation_id
        || completion->token.owner_generation != self->generation) {
        self->stale_completion_count++;
        return;
    }
    context->io_completion = *completion;
    context->io_completed = true;
}

/** @brief Select the target A/B slot based on scanned slot validity.
 *  @param self Storage service instance. */
static void choose_target(StorageService* self) {
    StorageServiceContext* context = &self->context;
    uint8_t expected_schema = context->slot_buffer[5];
    uint16_t expected_payload_size = le_read16(context->slot_buffer + 6u);
    SlotSelectionResult selected = ab_slot_select(context->scan_a,
                                                  context->slot_size,
                                                  context->scan_b,
                                                  context->slot_size,
                                                  context->record_type,
                                                  expected_schema,
                                                  expected_payload_size);
    context->target_slot = ab_slot_choose_target(
        selected.slot_a_valid, selected.slot_b_valid, selected.selected_slot);
    context->target_address = slot_addr_for(context->target_slot, context->record_type);
    context->write_offset = 0u;
    context->state = STORAGE_STATE_INVALIDATE;
}

/** @brief Decode scanned slot data into the restored volume structure.
 *  @param self Storage service instance. */
static void decode_restore(StorageService* self) {
    StorageServiceContext* context = &self->context;
    SlotSelectionResult selected = ab_slot_select(context->scan_a,
                                                  SLOT_VOLUME_SIZE,
                                                  context->scan_b,
                                                  SLOT_VOLUME_SIZE,
                                                  PERSIST_RECORD_VOLUME,
                                                  VOLUME_PAYLOAD_V1_SCHEMA,
                                                  VOLUME_PAYLOAD_V1_SIZE);
    memset(&self->restored_volume, 0, sizeof(self->restored_volume));
    if (selected.selected_slot == 0xFFu) {
        self->restore_status = STORAGE_RESTORE_EMPTY;
    } else {
        const uint8_t* slot = selected.selected_slot ? context->scan_b : context->scan_a;
        bool decoded =
            StorageRecord_DecodeVolume(slot,
                                       &self->restored_volume.forward_volume_ul,
                                       &self->restored_volume.reverse_volume_ul,
                                       &self->restored_volume.forward_remainder,
                                       &self->restored_volume.reverse_remainder,
                                       &self->restored_volume.state_version,
                                       &self->restored_volume.last_flow_sequence,
                                       &self->restored_volume.last_source_generation);
        self->restore_status = decoded ? STORAGE_RESTORE_OK : STORAGE_RESTORE_CORRUPT;
    }
    self->restore_ready = true;
    context->state = STORAGE_STATE_RESTORE_COMPLETE;
}

/** @brief Advance the storage FSM and submit at most one I/O operation.
 *  @param self Storage service instance.
 *  @param now_us Current time in microseconds. */
void StorageService_Tick(StorageService* self, uint64_t now_us) {
    if (!self || !storage_port_is_valid(&self->port))
        return;
    StorageServiceContext* context = &self->context;

    /* Consuming a completion is the bounded work for this turn. */
    if (context->io_pending) {
        (void)consume_io_completion(self);
        return;
    }

    if ((context->state == STORAGE_STATE_COMPLETE
         || context->state == STORAGE_STATE_FAILED)
        && context->pending && !self->completion_ready) {
        promote_pending(self);
        return;
    }

    switch (context->state) {
        case STORAGE_STATE_IDLE:
        case STORAGE_STATE_COMPLETE:
        case STORAGE_STATE_FAILED:
        case STORAGE_STATE_RESTORE_COMPLETE:
        case STORAGE_STATE_RESTORE_FAILED:
            break;

        case STORAGE_STATE_SCAN_A:
            if (!submit_read(self,
                             slot_addr_for(0u, context->record_type),
                             context->scan_a,
                             context->slot_size,
                             STORAGE_STATE_SCAN_B,
                             0u,
                             now_us))
                fail_io(self, STORAGE_IO_RESULT_BUS_ERROR);
            break;

        case STORAGE_STATE_SCAN_B:
            if (!submit_read(self,
                             slot_addr_for(1u, context->record_type),
                             context->scan_b,
                             context->slot_size,
                             STORAGE_STATE_PREPARE_TARGET,
                             0u,
                             now_us))
                fail_io(self, STORAGE_IO_RESULT_BUS_ERROR);
            break;

        case STORAGE_STATE_PREPARE_TARGET:
            choose_target(self);
            break;

        case STORAGE_STATE_INVALIDATE: {
            context->io_byte = PERSIST_COMMIT_INVALID;
            uint16_t address =
                (uint16_t)(context->target_address + context->slot_size - 1u);
            if (!submit_write(self,
                              address,
                              &context->io_byte,
                              1u,
                              STORAGE_STATE_VERIFY_INVALIDATE,
                              0u,
                              now_us))
                fail_io(self, STORAGE_IO_RESULT_BUS_ERROR);
            break;
        }

        case STORAGE_STATE_VERIFY_INVALIDATE: {
            uint16_t address =
                (uint16_t)(context->target_address + context->slot_size - 1u);
            if (!submit_read(self,
                             address,
                             &context->io_byte,
                             1u,
                             STORAGE_STATE_CHECK_INVALIDATE,
                             0u,
                             now_us))
                fail_io(self, STORAGE_IO_RESULT_BUS_ERROR);
            break;
        }

        case STORAGE_STATE_CHECK_INVALIDATE:
            if (context->io_byte == PERSIST_COMMIT_VALID)
                finish_commit(self, STORAGE_COMMIT_VERIFY_ERROR);
            else {
                context->write_offset = 0u;
                context->state = STORAGE_STATE_WRITE_BODY;
            }
            break;

        case STORAGE_STATE_WRITE_BODY: {
            uint16_t body_size = (uint16_t)(context->slot_size - 1u);
            if (context->write_offset >= body_size) {
                context->write_offset = 0u;
                context->state = STORAGE_STATE_READBACK_BODY;
                break;
            }
            uint16_t chunk = STORAGE_BODY_CHUNK_BYTES;
            if ((uint32_t)context->write_offset + chunk > body_size)
                chunk = (uint16_t)(body_size - context->write_offset);
            uint16_t address =
                (uint16_t)(context->target_address + context->write_offset);
            if (!submit_write(self,
                              address,
                              context->slot_buffer + context->write_offset,
                              chunk,
                              STORAGE_STATE_WRITE_BODY,
                              chunk,
                              now_us))
                fail_io(self, STORAGE_IO_RESULT_BUS_ERROR);
            break;
        }

        case STORAGE_STATE_READBACK_BODY: {
            uint16_t body_size = (uint16_t)(context->slot_size - 1u);
            if (context->write_offset >= body_size) {
                context->state = STORAGE_STATE_VERIFY_BODY;
                break;
            }
            uint16_t chunk = STORAGE_BODY_CHUNK_BYTES;
            if ((uint32_t)context->write_offset + chunk > body_size)
                chunk = (uint16_t)(body_size - context->write_offset);
            uint16_t address =
                (uint16_t)(context->target_address + context->write_offset);
            if (!submit_read(self,
                             address,
                             context->readback + context->write_offset,
                             chunk,
                             STORAGE_STATE_READBACK_BODY,
                             chunk,
                             now_us))
                fail_io(self, STORAGE_IO_RESULT_BUS_ERROR);
            break;
        }

        case STORAGE_STATE_VERIFY_BODY:
            if (memcmp(context->slot_buffer,
                       context->readback,
                       (size_t)(context->slot_size - 1u))
                != 0)
                finish_commit(self, STORAGE_COMMIT_VERIFY_ERROR);
            else
                context->state = STORAGE_STATE_COMMIT;
            break;

        case STORAGE_STATE_COMMIT: {
            context->io_byte = PERSIST_COMMIT_VALID;
            uint16_t address =
                (uint16_t)(context->target_address + context->slot_size - 1u);
            if (!submit_write(self,
                              address,
                              &context->io_byte,
                              1u,
                              STORAGE_STATE_VERIFY_COMMIT,
                              0u,
                              now_us))
                fail_io(self, STORAGE_IO_RESULT_BUS_ERROR);
            break;
        }

        case STORAGE_STATE_VERIFY_COMMIT: {
            uint16_t address =
                (uint16_t)(context->target_address + context->slot_size - 1u);
            if (!submit_read(self,
                             address,
                             &context->io_byte,
                             1u,
                             STORAGE_STATE_CHECK_COMMIT,
                             0u,
                             now_us))
                fail_io(self, STORAGE_IO_RESULT_BUS_ERROR);
            break;
        }

        case STORAGE_STATE_CHECK_COMMIT:
            if (context->io_byte != PERSIST_COMMIT_VALID)
                finish_commit(self, STORAGE_COMMIT_VERIFY_ERROR);
            else
                finish_commit(self, STORAGE_COMMIT_OK);
            break;

        case STORAGE_STATE_RESTORE_SCAN_A:
            if (!submit_read(self,
                             SLOT_VOLUME_A_ADDR,
                             context->scan_a,
                             SLOT_VOLUME_SIZE,
                             STORAGE_STATE_RESTORE_SCAN_B,
                             0u,
                             now_us))
                fail_io(self, STORAGE_IO_RESULT_BUS_ERROR);
            break;

        case STORAGE_STATE_RESTORE_SCAN_B:
            if (!submit_read(self,
                             SLOT_VOLUME_B_ADDR,
                             context->scan_b,
                             SLOT_VOLUME_SIZE,
                             STORAGE_STATE_RESTORE_DECODE,
                             0u,
                             now_us))
                fail_io(self, STORAGE_IO_RESULT_BUS_ERROR);
            break;

        case STORAGE_STATE_RESTORE_DECODE:
            decode_restore(self);
            break;
    }
}

/** @brief Take the most recent commit completion payload, if ready.
 *  @param self Storage service instance.
 *  @param completion_out Output for the completion payload.
 *  @return true if a completion was available and copied. */
bool StorageService_TakeCompletion(StorageService* self,
                                   StorageCompletionPayload* completion_out) {
    if (!self || !completion_out || !self->completion_ready)
        return false;
    *completion_out = self->last_completion;
    self->completion_ready = false;
    return true;
}

/** @brief Start restoring volume data from persistent storage.
 *  @param self Storage service instance.
 *  @return STORAGE_OK if accepted, or an error status. */
StorageStatus StorageService_StartRestoreVolume(StorageService* self) {
    if (!self || !storage_port_is_valid(&self->port))
        return STORAGE_REJECTED;
    if (!is_terminal(self->context.state) || self->context.pending
        || self->completion_ready)
        return STORAGE_BUSY;
    memset(self->context.scan_a, 0, sizeof(self->context.scan_a));
    memset(self->context.scan_b, 0, sizeof(self->context.scan_b));
    self->context.write_offset = 0u;
    self->restore_ready = false;
    self->context.state = STORAGE_STATE_RESTORE_SCAN_A;
    return STORAGE_OK;
}

/** @brief Take restored volume data from a completed restore operation.
 *  @param self Storage service instance.
 *  @param status_out Output for the restore status.
 *  @param volume_out Output for the restored volume data.
 *  @return true if restored data was available and copied. */
bool StorageService_TakeRestoredVolume(StorageService* self,
                                       StorageRestoreStatus* status_out,
                                       StorageRestoredVolume* volume_out) {
    if (!self || !status_out || !volume_out || !self->restore_ready)
        return false;
    *status_out = self->restore_status;
    *volume_out = self->restored_volume;
    self->restore_ready = false;
    self->context.state = STORAGE_STATE_IDLE;
    return true;
}
