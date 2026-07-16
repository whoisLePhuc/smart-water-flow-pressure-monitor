#include "services/storage_service.h"
#include "services/fram_driver.h"
#include <string.h>

typedef enum {
    ST_IDLE,
    ST_ENCODE,
    ST_INVALIDATE,
    ST_VERIFY_INVALIDATE,
    ST_WRITE_BODY,
    ST_READBACK_BODY,
    ST_VERIFY_BODY,
    ST_COMMIT,
    ST_VERIFY_COMMIT,
    ST_COMPLETE,
    ST_FAILED
} StState;

#define STORAGE_MAX_SLOT_SIZE SLOT_CALIBRATION_SIZE

typedef struct {
    StState  state;
    uint8_t  rec_type;
    uint32_t seq;
    uint64_t cand_ver;
    uint16_t enc_len;
    uint8_t  slot_buf[STORAGE_MAX_SLOT_SIZE];
    uint8_t  readback[STORAGE_MAX_SLOT_SIZE];
    uint8_t  tgt_slot;
    uint16_t tgt_addr;
    uint16_t slt_size;
    uint16_t wr_off;
    bool     pending;
    uint8_t  pend_buf[STORAGE_MAX_SLOT_SIZE];
    uint16_t pend_len;
    uint64_t pend_ver;
    uint32_t pend_seq;
    uint8_t  pend_type;
} StCtx;

struct StorageServiceImpl {
    FramDriver *fram;
    StCtx       ctx;
    uint32_t    generation;
    uint64_t    req_cnt;
};

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
    StCtx *c = &s->ctx;
    switch (c->state) {

    case ST_IDLE: break;

    case ST_ENCODE:
        c->state = ST_INVALIDATE;
        break;

    case ST_INVALIDATE: {
        uint8_t v = PERSIST_COMMIT_INVALID;
        uint16_t commit_addr = (uint16_t)((uint32_t)c->tgt_addr
            + (uint32_t)c->slt_size - 1u);
        if (FramDriver_Write(s->fram, commit_addr, &v, 1u) != FRAM_DRV_OK) {
            c->state = ST_FAILED; break;
        }
        c->wr_off = 0;
        c->state = ST_VERIFY_INVALIDATE;
        break;
    }

    case ST_VERIFY_INVALIDATE: {
        uint8_t rb;
        uint16_t commit_addr = (uint16_t)((uint32_t)c->tgt_addr
            + (uint32_t)c->slt_size - 1u);
        if (FramDriver_Read(s->fram, commit_addr, &rb, 1u) != FRAM_DRV_OK) {
            c->state = ST_FAILED; break;
        }
        c->state = (rb == PERSIST_COMMIT_VALID) ? ST_FAILED : ST_WRITE_BODY;
        break;
    }

    case ST_WRITE_BODY: {
        uint16_t body = (uint16_t)(c->slt_size - 1u);
        if (c->wr_off >= body) { c->state = ST_READBACK_BODY; c->wr_off = 0; break; }
        uint16_t ch = 32;
        if ((uint32_t)c->wr_off + (uint32_t)ch > (uint32_t)body)
            ch = (uint16_t)(body - c->wr_off);
        uint16_t write_addr = (uint16_t)((uint32_t)c->tgt_addr
            + (uint32_t)c->wr_off);
        if (FramDriver_Write(s->fram, write_addr,
                             c->slot_buf + c->wr_off, ch) != FRAM_DRV_OK) {
            c->state = ST_FAILED; break;
        }
        c->wr_off = (uint16_t)(c->wr_off + ch);
        break;
    }

    case ST_READBACK_BODY: {
        uint16_t body = (uint16_t)(c->slt_size - 1u);
        if (c->wr_off >= body) { c->state = ST_VERIFY_BODY; c->wr_off = 0; break; }
        uint16_t ch = 32;
        if ((uint32_t)c->wr_off + (uint32_t)ch > (uint32_t)body)
            ch = (uint16_t)(body - c->wr_off);
        uint16_t read_addr = (uint16_t)((uint32_t)c->tgt_addr
            + (uint32_t)c->wr_off);
        if (FramDriver_Read(s->fram, read_addr,
                            c->readback + c->wr_off, ch) != FRAM_DRV_OK) {
            c->state = ST_FAILED; break;
        }
        c->wr_off = (uint16_t)(c->wr_off + ch);
        break;
    }

    case ST_VERIFY_BODY:
        if (memcmp(c->slot_buf, c->readback,
                   (size_t)(c->slt_size - 1u)) != 0) {
            c->state = ST_FAILED; break;
        }
        c->state = ST_COMMIT;
        break;

    case ST_COMMIT: {
        uint8_t v = PERSIST_COMMIT_VALID;
        uint16_t commit_addr = (uint16_t)((uint32_t)c->tgt_addr
            + (uint32_t)c->slt_size - 1u);
        if (FramDriver_Write(s->fram, commit_addr, &v, 1u) != FRAM_DRV_OK) {
            c->state = ST_FAILED; break;
        }
        c->state = ST_VERIFY_COMMIT;
        break;
    }

    case ST_VERIFY_COMMIT: {
        uint8_t rb;
        uint16_t commit_addr = (uint16_t)((uint32_t)c->tgt_addr
            + (uint32_t)c->slt_size - 1u);
        if (FramDriver_Read(s->fram, commit_addr, &rb, 1u) != FRAM_DRV_OK) {
            c->state = ST_FAILED; break;
        }
        c->state = (rb == PERSIST_COMMIT_VALID) ? ST_COMPLETE : ST_FAILED;
        break;
    }

    case ST_COMPLETE:
    case ST_FAILED:
        break;
    }
}

static void promote(struct StorageServiceImpl *s)
{
    StCtx *c = &s->ctx;
    c->rec_type = c->pend_type;
    c->seq = c->pend_seq;
    c->cand_ver = c->pend_ver;
    c->enc_len = c->pend_len;
    c->slt_size = slot_size_for(c->pend_type);
    c->tgt_addr = slot_addr_for(0, c->pend_type);
    if (!c->slt_size || !c->tgt_addr) return;
    memset(c->slot_buf, 0, sizeof(c->slot_buf));
    if (c->pend_len && c->pend_len <= sizeof(c->slot_buf))
        memcpy(c->slot_buf, c->pend_buf, c->pend_len);
    c->pending = false;
    c->state = ST_ENCODE;
}

void StorageService_Init(StorageService *self)
{
    memset(self, 0, sizeof(*self));
    self->ctx.state = ST_IDLE;
    self->generation = 1;
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
    StCtx *c = &self->ctx;
    if (c->state == ST_IDLE || c->state == ST_COMPLETE) {
        c->rec_type = record_type;
        c->seq = sequence;
        c->cand_ver = candidate_version;
        c->slt_size = slot_size_for(record_type);
        c->tgt_addr = slot_addr_for(0, record_type);
        if (!c->slt_size || !c->tgt_addr) return STORAGE_REJECTED;
        c->enc_len = encoded_length < c->slt_size ? encoded_length : c->slt_size;
        memset(c->slot_buf, 0, sizeof(c->slot_buf));
        memcpy(c->slot_buf, encoded_buffer, c->enc_len);
        c->state = ST_ENCODE;
        c->pending = false;
        return STORAGE_OK;
    }
    c->pending = true;
    c->pend_type = record_type;
    c->pend_seq = sequence;
    c->pend_ver = candidate_version;
    c->pend_len = encoded_length < sizeof(c->pend_buf) ? encoded_length : sizeof(c->pend_buf);
    memset(c->pend_buf, 0, sizeof(c->pend_buf));
    if (c->pend_len) memcpy(c->pend_buf, encoded_buffer, c->pend_len);
    return STORAGE_BUSY;
}

void StorageService_Tick(StorageService *self)
{
    if (!self || !self->fram) return;
    StCtx *c = &self->ctx;
    tick_fsm(self);
    if (c->state == ST_COMPLETE && c->pending) promote(self);
    if (c->state == ST_FAILED && c->pending) promote(self);
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
