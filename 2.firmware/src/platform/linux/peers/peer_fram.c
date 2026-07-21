#include "peers/peer_fram.h"
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

    if (!tx || tx_len < 1u) return false;

    uint16_t addr = (uint16_t)(((uint16_t)(slave_addr & 0x01u) << 8u) |
                               tx[0]);

    /* Write */
    if (tx_len > 1u) {
        uint16_t data_length = (uint16_t)(tx_len - 1u);
        if (data_length > FRAM_PEER_SIZE - addr)
            return false;
        memcpy(peer->memory + addr, tx + 1u, data_length);
        peer->write_count++;
        return true;
    }

    /* Read */
    if (rx && rx_len > 0) {
        if (rx_len > FRAM_PEER_SIZE - addr)
            return false;
        memcpy(rx, peer->memory + addr, rx_len);
        peer->read_count++;
        return true;
    }

    peer->error_count++;
    return false;
}
