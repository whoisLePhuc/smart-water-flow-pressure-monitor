#include "platform/peers/peer_fram.h"
#include <string.h>

void fram_peer_init(FramPeer *peer)
{
    memset(peer, 0, sizeof(*peer));
    peer->generation = 1;
    peer->i2c_latency_us = 20;
}

bool fram_peer_plan_i2c(void *context, uint8_t slave_addr,
                         const uint8_t *tx, uint16_t tx_len,
                         uint8_t *rx, uint16_t rx_len,
                         uint64_t *latency_us, uint32_t *status_flags)
{
    FramPeer *peer = (FramPeer *)context;
    if (!peer || !latency_us || !status_flags) return false;

    (void)slave_addr;
    *latency_us = peer->i2c_latency_us;
    *status_flags = 0;

    if (!tx || tx_len < 2) return false;

    uint16_t addr = (uint16_t)(tx[0] << 8) | tx[1];

    /* Write */
    if (tx_len > 2) {
        for (uint16_t i = 2; i < tx_len; i++) {
            if (addr + (i - 2) < FRAM_PEER_SIZE) {
                peer->memory[addr + (i - 2)] = tx[i];
            }
        }
        peer->write_count++;
        return true;
    }

    /* Read */
    if (rx && rx_len > 0) {
        for (uint16_t i = 0; i < rx_len && addr + i < FRAM_PEER_SIZE; i++) {
            rx[i] = peer->memory[addr + i];
        }
        peer->read_count++;
        return true;
    }

    peer->error_count++;
    return false;
}
