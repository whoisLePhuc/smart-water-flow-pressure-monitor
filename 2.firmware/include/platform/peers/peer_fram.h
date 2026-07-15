#ifndef SWFPM_PEER_FRAM_H
#define SWFPM_PEER_FRAM_H

#include <stdint.h>
#include <stdbool.h>

/* Minimal F-RAM peer for shared-I2C arbitration testing.
 * Implements: read, write, acknowledge. */

#define FRAM_PEER_SIZE 512

typedef struct {
    uint8_t  memory[FRAM_PEER_SIZE];
    uint32_t generation;
    uint64_t i2c_latency_us;
    uint32_t read_count;
    uint32_t write_count;
    uint32_t error_count;
} FramPeer;

void fram_peer_init(FramPeer *peer);

bool fram_peer_plan_i2c(void *context, uint8_t slave_addr,
                         const uint8_t *tx, uint16_t tx_len,
                         uint8_t *rx, uint16_t rx_len,
                         uint64_t *latency_us, uint32_t *status_flags);

#endif
