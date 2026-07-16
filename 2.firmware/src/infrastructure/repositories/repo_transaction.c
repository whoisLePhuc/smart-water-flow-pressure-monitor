#include "repo_transaction.h"
#include "data_repository.h"
#include <string.h>
#include <stdatomic.h>

void txn_init(RepoWriteTxn *txn)
{
    if (txn) memset(txn, 0, sizeof(*txn));
}

bool txn_begin(RepoWriteTxn *txn, DataRepository *repo)
{
    if (!txn || !repo || txn->state == TXN_STATE_ACTIVE) return false;

    uint8_t active = atomic_load_explicit(&repo->active_index, memory_order_acquire);
    memcpy(&repo->inactive_buffer, &repo->buffers[active], sizeof(RuntimeSnapshot));
    repo->accept_in_progress = true;

    txn->repo = repo;
    txn->base_generation = repo->snapshot_version;
    txn->written_fields_mask = 0;
    txn->state = TXN_STATE_ACTIVE;
    return true;
}

bool txn_commit(RepoWriteTxn *txn)
{
    if (!txn) return false;
    if (txn->state != TXN_STATE_ACTIVE) {
        txn->state = TXN_STATE_ERROR;
        return false;
    }
    DataRepository *repo = txn->repo;

    repo->snapshot_version++;
    repo->inactive_buffer.snapshot_version = repo->snapshot_version;

    uint8_t inactive_idx = atomic_load_explicit(&repo->active_index, memory_order_acquire) ^ 1U;
    memcpy(&repo->buffers[inactive_idx], &repo->inactive_buffer, sizeof(RuntimeSnapshot));
    atomic_store_explicit(&repo->active_index, inactive_idx, memory_order_release);

    repo->accept_in_progress = false;
    repo->publish_pending = false;
    txn->state = TXN_STATE_COMMITTED;
    return true;
}

void txn_abort(RepoWriteTxn *txn)
{
    if (!txn || txn->state != TXN_STATE_ACTIVE) return;
    DataRepository *repo = txn->repo;
    memset(&repo->inactive_buffer, 0, sizeof(RuntimeSnapshot));
    repo->accept_in_progress = false;
    repo->publish_pending = false;
    txn->written_fields_mask = 0;
    txn->state = TXN_STATE_ABORTED;
}

bool txn_read_snapshot(const RepoWriteTxn *txn, RuntimeSnapshot *snapshot_out)
{
    if (!txn || !txn->repo || !snapshot_out) return false;
    uint8_t active = atomic_load_explicit(&txn->repo->active_index, memory_order_acquire);
    memcpy(snapshot_out, &txn->repo->buffers[active], sizeof(RuntimeSnapshot));
    return true;
}

/* ── Typed writes (all follow same pattern) ── */

static bool write_field(RepoWriteTxn *txn, const void *data, size_t size,
                         size_t offset, uint32_t mask_bit)
{
    if (!txn || !data) return false;
    if (txn->state != TXN_STATE_ACTIVE) {
        txn->state = TXN_STATE_ERROR;
        return false;
    }
    uint8_t *base = (uint8_t *)&txn->repo->inactive_buffer;
    memcpy(base + offset, data, size);
    txn->written_fields_mask |= mask_bit;
    return true;
}

#define FIELD_OFFSET(type, field) ((size_t)&((type *)0)->field)
#define FLOW_BIT    (1u << 0)
#define PRESSURE_BIT (1u << 1)
#define TEMP_BIT    (1u << 2)
#define VOLUME_BIT  (1u << 3)
#define LEAK_BIT    (1u << 4)
#define MODE_BIT    (1u << 5)
#define POWER_BIT   (1u << 6)

bool txn_write_flow(RepoWriteTxn *txn, const FlowResult *result)
{
    return write_field(txn, result, sizeof(FlowResult),
                       FIELD_OFFSET(RuntimeSnapshot, flow), FLOW_BIT);
}

bool txn_write_pressure(RepoWriteTxn *txn, const PressureResult *result)
{
    return write_field(txn, result, sizeof(PressureResult),
                       FIELD_OFFSET(RuntimeSnapshot, pressure), PRESSURE_BIT);
}

bool txn_write_temperature(RepoWriteTxn *txn, const TemperatureResult *result)
{
    return write_field(txn, result, sizeof(TemperatureResult),
                       FIELD_OFFSET(RuntimeSnapshot, temperature), TEMP_BIT);
}

bool txn_write_volume(RepoWriteTxn *txn, const VolumeState *state)
{
    return write_field(txn, state, sizeof(VolumeState),
                       FIELD_OFFSET(RuntimeSnapshot, volume), VOLUME_BIT);
}

bool txn_write_leak(RepoWriteTxn *txn, const LeakDetectionResult *leak)
{
    return write_field(txn, leak, sizeof(LeakDetectionResult),
                       FIELD_OFFSET(RuntimeSnapshot, leak), LEAK_BIT);
}

bool txn_write_mode(RepoWriteTxn *txn, const SystemModeContext *mode)
{
    return write_field(txn, mode, sizeof(SystemModeContext),
                       FIELD_OFFSET(RuntimeSnapshot, mode), MODE_BIT);
}

bool txn_write_power(RepoWriteTxn *txn, const PowerSnapshot *power)
{
    return write_field(txn, power, sizeof(PowerSnapshot),
                       FIELD_OFFSET(RuntimeSnapshot, power), POWER_BIT);
}
