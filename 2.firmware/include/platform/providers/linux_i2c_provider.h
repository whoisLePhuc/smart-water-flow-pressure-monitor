#ifndef SWFPM_LINUX_I2C_PROVIDER_H
#define SWFPM_LINUX_I2C_PROVIDER_H

#include <stdint.h>
#include <stdbool.h>
#include "platform/linux_scheduled_action_queue.h"

#define LINUX_I2C_MAX_PEERS 8

/* ── I2C Request ─────────────────────────────────────────── */

typedef struct {
    uint32_t operation_id;
    uint32_t correlation_id;
    uint32_t owner_generation;
    uint64_t deadline_us;
    uint8_t  slave_address;
    const uint8_t *tx_data;
    uint16_t tx_length;
    uint8_t *rx_data;
    uint16_t rx_length;
} LinuxI2cRequest;

/* ── Peer interface ──────────────────────────────────────── */

typedef struct {
    bool (*i2c_plan)(void *context,
                     uint8_t slave_addr,
                     const uint8_t *tx, uint16_t tx_len,
                     uint8_t *rx, uint16_t rx_len,
                     uint64_t *latency_us,
                     uint32_t *status_flags);
    void *context;
} LinuxI2cPeer;

/* ── Provider ────────────────────────────────────────────── */

typedef struct {
    LinuxScheduledActionQueue *action_queue;
    uint8_t                    peer_count;
    struct {
        uint8_t address;
        LinuxI2cPeer peer;
    } peers[LINUX_I2C_MAX_PEERS];
    uint32_t                   resource_generation;
    bool                       active;
    uint32_t                   active_op_id;
    uint32_t                   admission_accepted;
    uint32_t                   admission_rejected;
    uint32_t                   completion_count;
} LinuxI2cProvider;

void linux_i2c_init(LinuxI2cProvider *provider,
                    LinuxScheduledActionQueue *action_queue);

bool linux_i2c_register_peer(LinuxI2cProvider *provider,
                             uint8_t address, LinuxI2cPeer peer);

bool linux_i2c_submit(LinuxI2cProvider *provider,
                      const LinuxI2cRequest *request);

bool linux_i2c_cancel(LinuxI2cProvider *provider,
                      uint32_t operation_id,
                      uint32_t expected_generation);

void linux_i2c_recover(LinuxI2cProvider *provider);

#endif
