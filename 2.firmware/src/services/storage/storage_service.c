#include "services/storage/storage_service.h"
#include "drivers/storage/fram_driver.h"
#include <string.h>

static uint16_t slot_addr_for(uint8_t slot, uint8_t type)
{
    if (type == PERSIST_RECORD_VOLUME) return slot ? SLOT_VOLUME_B_ADDR : SLOT_VOLUME_A_ADDR;
    if (type == PERSIST_RECORD_CONFIG) return slot ? SLOT_CONFIG_B_ADDR   : SLOT_CONFIG_A_ADDR;
    if (type == PERSIST_RECORD_CALIBRATION) return slot ? SLOT_CALIBRATION_B_ADDR : SLOT_CALIBRATION_A_ADDR;
    return 0;
}

static uint16_t slot_size_for(uint8_t type)
{
    if (type == PERSIST_RECORD_VOLUME) return SLOT_VOLUME_SIZE;
    if (type == PERSIST_RECORD_CONFIG) return SLOT_CONFIG_SIZE;
    if (type == PERSIST_RECORD_CALIBRATION) return SLOT_CALIBRATION_SIZE;
    return 0;
}

static void tick_fsm(struct StorageServiceImpl *s)
{
    StorageServiceContext *c = &s->context;
    switch (c->state) {

    case STORAGE_STATE_IDLE: break;

    case STORAGE_STATE_ENCODE:
        c->state = STORAGE_STATE_INVALIDATE;
        break;

    case STORAGE_STATE_INVALIDATE: {
        uint8_t v = PERSIST_COMMIT_INVALID;
        uint16_t commit_addr = (uint16_t)((uint32_t)c->target_address
            + (uint32_t)c->slot_size - 1u);
        if (FramDriver_Write(s->fram, commit_addr, &v, 1u) != FRAM_DRV_OK) {
            c->state = STORAGE_STATE_FAILED; break;
        }
        c->write_offset = 0;
        c->state = STORAGE_STATE_VERIFY_INVALIDATE;
        break;
    }

    case STORAGE_STATE_VERIFY_INVALIDATE: {
        uint8_t rb;
        uint16_t commit_addr = (uint16_t)((uint32_t)c->target_address
            + (uint32_t)c->slot_size - 1u);
        if (FramDriver_Read(s->fram, commit_addr, &rb, 1u) != FRAM_DRV_OK) {
            c->state = STORAGE_STATE_FAILED; break;
        }
        c->state = (rb == PERSIST_COMMIT_VALID) ? STORAGE_STATE_FAILED : STORAGE_STATE_WRITE_BODY;
        break;
    }

    case STORAGE_STATE_WRITE_BODY: {
        uint16_t body = (uint16_t)(c->slot_size - 1u);
        if (c->write_offset >= body) { c->state = STORAGE_STATE_READBACK_BODY; c->write_offset = 0; break; }
        uint16_t ch = 32;
        if ((uint32_t)c->write_offset + (uint32_t)ch > (uint32_t)body)
            ch = (uint16_t)(body - c->write_offset);
        uint16_t write_addr = (uint16_t)((uint32_t)c->target_address
            + (uint32_t)c->write_offset);
        if (FramDriver_Write(s->fram, write_addr,
                             c->slot_buffer + c->write_offset, ch) != FRAM_DRV_OK) {
            c->state = STORAGE_STATE_FAILED; break;
        }
        c->write_offset = (uint16_t)(c->write_offset + ch);
        break;
    }

    case STORAGE_STATE_READBACK_BODY: {
        uint16_t body = (uint16_t)(c->slot_size - 1u);
        if (c->write_offset >= body) { c->state = STORAGE_STATE_VERIFY_BODY; c->write_offset = 0; break; }
        uint16_t ch = 32;
        if ((uint32_t)c->write_offset + (uint32_t)ch > (uint32_t)body)
            ch = (uint16_t)(body - c->write_offset);
        uint16_t read_addr = (uint16_t)((uint32_t)c->target_address
            + (uint32_t)c->write_offset);
        if (FramDriver_Read(s->fram, read_addr,
                            c->readback + c->write_offset, ch) != FRAM_DRV_OK) {
            c->state = STORAGE_STATE_FAILED; break;
        }
        c->write_offset = (uint16_t)(c->write_offset + ch);
        break;
    }

    case STORAGE_STATE_VERIFY_BODY:
        if (memcmp(c->slot_buffer, c->readback,
                   (size_t)(c->slot_size - 1u)) != 0) {
            c->state = STORAGE_STATE_FAILED; break;
        }
        c->state = STORAGE_STATE_COMMIT;
        break;

    case STORAGE_STATE_COMMIT: {
        uint8_t v = PERSIST_COMMIT_VALID;
        uint16_t commit_addr = (uint16_t)((uint32_t)c->target_address
            + (uint32_t)c->slot_size - 1u);
        if (FramDriver_Write(s->fram, commit_addr, &v, 1u) != FRAM_DRV_OK) {
            c->state = STORAGE_STATE_FAILED; break;
        }
        c->state = STORAGE_STATE_VERIFY_COMMIT;
        break;
    }

    case STORAGE_STATE_VERIFY_COMMIT: {
        uint8_t rb;
        uint16_t commit_addr = (uint16_t)((uint32_t)c->target_address
            + (uint32_t)c->slot_size - 1u);
        if (FramDriver_Read(s->fram, commit_addr, &rb, 1u) != FRAM_DRV_OK) {
            c->state = STORAGE_STATE_FAILED; break;
        }
        c->state = (rb == PERSIST_COMMIT_VALID) ? STORAGE_STATE_COMPLETE : STORAGE_STATE_FAILED;
        break;
    }

    case STORAGE_STATE_COMPLETE:
    case STORAGE_STATE_FAILED:
        break;
    }
}

static void promote(struct StorageServiceImpl *s)
{
    StorageServiceContext *c = &s->context;
    c->record_type = c->pending_type;
    c->sequence = c->pending_sequence;
    c->candidate_version = c->pending_version;
    c->encoded_length = c->pending_length;
    c->slot_size = slot_size_for(c->pending_type);
    c->target_address = slot_addr_for(0, c->pending_type);
    if (!c->slot_size) return;
    memset(c->slot_buffer, 0, sizeof(c->slot_buffer));
    if (c->pending_length && c->pending_length <= sizeof(c->slot_buffer))
        memcpy(c->slot_buffer, c->pending_buffer, c->pending_length);
    c->pending = false;
    c->state = STORAGE_STATE_ENCODE;
}

StorageStatus StorageService_Init(StorageService *self, FramDriver *fram)
{
    if (!self || !fram) return STORAGE_REJECTED;
    memset(self, 0, sizeof(*self));
    self->fram = fram;
    self->context.state = STORAGE_STATE_IDLE;
    self->generation = 1;
    return STORAGE_OK;
}

StorageStatus StorageService_SubmitCheckpoint(
    StorageService *self,
    uint8_t         record_type,
    uint32_t        sequence,
    const uint8_t  *encoded_buffer,
    uint16_t        encoded_length,
    uint64_t        candidate_version)
{
    if (!self || !encoded_buffer || !encoded_length)
        return STORAGE_REJECTED;
    StorageServiceContext *c = &self->context;
    if (c->state == STORAGE_STATE_IDLE || c->state == STORAGE_STATE_COMPLETE) {
        c->record_type = record_type;
        c->sequence = sequence;
        c->candidate_version = candidate_version;
        c->slot_size = slot_size_for(record_type);
        c->target_address = slot_addr_for(0, record_type);
        if (!c->slot_size) return STORAGE_REJECTED;
        c->encoded_length = encoded_length < c->slot_size ? encoded_length : c->slot_size;
        memset(c->slot_buffer, 0, sizeof(c->slot_buffer));
        memcpy(c->slot_buffer, encoded_buffer, c->encoded_length);
        c->state = STORAGE_STATE_ENCODE;
        c->pending = false;
        return STORAGE_OK;
    }
    c->pending = true;
    c->pending_type = record_type;
    c->pending_sequence = sequence;
    c->pending_version = candidate_version;
    c->pending_length = encoded_length < sizeof(c->pending_buffer) ? encoded_length : sizeof(c->pending_buffer);
    memset(c->pending_buffer, 0, sizeof(c->pending_buffer));
    if (c->pending_length) memcpy(c->pending_buffer, encoded_buffer, c->pending_length);
    return STORAGE_BUSY;
}

void StorageService_Tick(StorageService *self)
{
    if (!self || !self->fram) return;
    StorageServiceContext *c = &self->context;
    tick_fsm(self);
    if (c->state == STORAGE_STATE_COMPLETE && c->pending) promote(self);
    if (c->state == STORAGE_STATE_FAILED && c->pending) promote(self);
}

StorageRestoreStatus StorageService_RestoreVolume(
    StorageService  *self,
    uint64_t        *fwd,
    uint64_t        *rev,
    uint64_t        *fwd_rem,
    uint64_t        *rev_rem,
    uint64_t        *sv,
    uint64_t        *lfs,
    uint32_t        *lsg)
{
    if (!self || !self->fram) {
        if (fwd) *fwd = 0;
        if (rev) *rev = 0;
        if (fwd_rem) *fwd_rem = 0;
        if (rev_rem) *rev_rem = 0;
        if (sv) *sv = 0;
        if (lfs) *lfs = 0;
        if (lsg) *lsg = 0;
        return STORAGE_RESTORE_EMPTY;
    }
    uint8_t a[SLOT_VOLUME_SIZE], b[SLOT_VOLUME_SIZE];
    if (FramDriver_Read(self->fram, SLOT_VOLUME_A_ADDR, a, SLOT_VOLUME_SIZE) != FRAM_DRV_OK)
        return STORAGE_RESTORE_IO_ERROR;
    if (FramDriver_Read(self->fram, SLOT_VOLUME_B_ADDR, b, SLOT_VOLUME_SIZE) != FRAM_DRV_OK)
        return STORAGE_RESTORE_IO_ERROR;
    SlotSelectionResult sel = ab_slot_select(a, SLOT_VOLUME_SIZE, b, SLOT_VOLUME_SIZE,
        PERSIST_RECORD_VOLUME, VOLUME_PAYLOAD_V1_SCHEMA, VOLUME_PAYLOAD_V1_SIZE);
    if (sel.selected_slot == 0xFF) {
        if (fwd) *fwd = 0;
        return STORAGE_RESTORE_EMPTY;
    }
    const uint8_t *slot = sel.selected_slot ? b : a;
    if (!StorageRecord_DecodeVolume(slot, fwd, rev, fwd_rem, rev_rem, sv, lfs, lsg))
        return STORAGE_RESTORE_CORRUPT;
    return STORAGE_RESTORE_OK;
}
