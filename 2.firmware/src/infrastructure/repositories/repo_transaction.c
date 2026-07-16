#include "repo_transaction.h"
#include "infrastructure/repositories/data_repository.h"
#include <string.h>
#include <stdatomic.h>
#include <stddef.h>

void txn_init(RepoWriteTxn *txn)
{
    if (txn) memset(txn, 0, sizeof(*txn));
}

bool txn_begin(RepoWriteTxn *txn, DataRepository *repo)
{
    if (!txn || !repo || txn->state == TXN_STATE_ACTIVE || repo->writer_active)
        return false;

    uint_fast8_t active = atomic_load_explicit(
        &repo->active_index, memory_order_acquire);
    txn->inactive_index = (uint8_t)(active ^ (uint_fast8_t)1);
    memcpy(&repo->buffers[txn->inactive_index],
           &repo->buffers[(uint8_t)active],
           sizeof(RuntimeSnapshot));

    txn->repo = repo;
    txn->base_generation = repo->snapshot_version;
    txn->written_fields_mask = 0;
    txn->state = TXN_STATE_ACTIVE;
    repo->write_index = txn->inactive_index;
    repo->writer_active = true;
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
    if (!repo || !repo->writer_active
        || repo->write_index != txn->inactive_index
        || repo->snapshot_version != txn->base_generation) {
        if (repo && repo->writer_active
            && repo->write_index == txn->inactive_index)
            repo->writer_active = false;
        txn->state = TXN_STATE_ERROR;
        return false;
    }

    repo->snapshot_version++;
    repo->buffers[txn->inactive_index].snapshot_version = repo->snapshot_version;
    atomic_store_explicit(&repo->active_index,
                          (uint_fast8_t)txn->inactive_index,
                          memory_order_release);
    repo->writer_active = false;

    txn->state = TXN_STATE_COMMITTED;
    return true;
}

void txn_abort(RepoWriteTxn *txn)
{
    if (!txn || txn->state != TXN_STATE_ACTIVE) return;
    DataRepository *repo = txn->repo;
    if (repo && repo->writer_active && repo->write_index == txn->inactive_index)
        repo->writer_active = false;
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


static bool write_field(RepoWriteTxn *txn, const void *data, size_t size,
                         size_t offset, uint32_t mask_bit)
{
    if (!txn || !data) return false;
    if (txn->state != TXN_STATE_ACTIVE) {
        txn->state = TXN_STATE_ERROR;
        return false;
    }
    if (!txn->repo || !txn->repo->writer_active
        || txn->repo->write_index != txn->inactive_index)
        return false;
    uint8_t *base = (uint8_t *)&txn->repo->buffers[txn->inactive_index];
    memcpy(base + offset, data, size);
    txn->written_fields_mask |= mask_bit;
    return true;
}

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
                       offsetof(RuntimeSnapshot, flow), FLOW_BIT);
}

bool txn_write_pressure(RepoWriteTxn *txn, const PressureResult *result)
{
    return write_field(txn, result, sizeof(PressureResult),
                       offsetof(RuntimeSnapshot, pressure), PRESSURE_BIT);
}

bool txn_write_temperature(RepoWriteTxn *txn, const TemperatureResult *result)
{
    return write_field(txn, result, sizeof(TemperatureResult),
                       offsetof(RuntimeSnapshot, temperature), TEMP_BIT);
}

bool txn_write_volume(RepoWriteTxn *txn, const VolumeState *state)
{
    return write_field(txn, state, sizeof(VolumeState),
                       offsetof(RuntimeSnapshot, volume), VOLUME_BIT);
}

bool txn_write_leak(RepoWriteTxn *txn, const LeakDetectionResult *leak)
{
    return write_field(txn, leak, sizeof(LeakDetectionResult),
                       offsetof(RuntimeSnapshot, leak), LEAK_BIT);
}

bool txn_write_mode(RepoWriteTxn *txn, const SystemModeContext *mode)
{
    return write_field(txn, mode, sizeof(SystemModeContext),
                       offsetof(RuntimeSnapshot, mode), MODE_BIT);
}

bool txn_write_power(RepoWriteTxn *txn, const PowerSnapshot *power)
{
    return write_field(txn, power, sizeof(PowerSnapshot),
                       offsetof(RuntimeSnapshot, power), POWER_BIT);
}
