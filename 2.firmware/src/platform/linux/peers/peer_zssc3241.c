#include "platform/peers/peer_zssc3241.h"
#include <string.h>

void zssc_peer_init(Zssc3241Peer *peer)
{
    memset(peer, 0, sizeof(*peer));
    peer->state = ZSSC_PEER_SLEEP;
    peer->generation = 1;
    peer->conversion_latency_us = 10000; /* 10 ms typical */
    peer->i2c_latency_us = 30;
    peer->fault = ZSSC_FAULT_NONE;
}

void zssc_peer_reset(Zssc3241Peer *peer, bool unexpected)
{
    if (!peer) return;
    peer->generation++;
    peer->eoc_active = false;
    if (unexpected) {
        peer->state = ZSSC_PEER_FAULT;
    } else {
        peer->state = ZSSC_PEER_SLEEP;
    }
}

uint64_t zssc_peer_schedule_eoc(Zssc3241Peer *peer, uint64_t start_us)
{
    if (!peer) return 0;

    peer->state = ZSSC_PEER_CONVERTING;
    peer->conversion_count++;

    if (peer->fault == ZSSC_FAULT_MISSING_EOC) {
        peer->eoc_active = false;
        return start_us + peer->conversion_latency_us;
    }

    peer->eoc_active = true;
    return start_us + peer->conversion_latency_us;
}

void zssc_peer_clear_eoc(Zssc3241Peer *peer)
{
    if (!peer) return;
    peer->eoc_active = false;
}

bool zssc_peer_is_eoc_active(const Zssc3241Peer *peer)
{
    return peer ? peer->eoc_active : false;
}

void zssc_peer_set_fault(Zssc3241Peer *peer, ZsscPeerFault fault)
{
    if (!peer) return;
    peer->fault = fault;
}

bool zssc_peer_plan_i2c(void *context, uint8_t slave_addr,
                         const uint8_t *tx, uint16_t tx_len,
                         uint8_t *rx, uint16_t rx_len,
                         uint64_t *latency_us, uint32_t *status_flags)
{
    Zssc3241Peer *peer = (Zssc3241Peer *)context;
    if (!peer || !latency_us || !status_flags) return false;

    (void)slave_addr;

    if (peer->fault == ZSSC_FAULT_NO_COMPLETION) {
        return false;
    }

    if (peer->fault == ZSSC_FAULT_STUCK_BUSY) {
        *status_flags = 0x80; /* Busy bit set */
        *latency_us = peer->i2c_latency_us;
        return true;
    }

    peer->i2c_operations++;
    *latency_us = peer->i2c_latency_us;
    *status_flags = 0;

    if (tx && tx_len > 0) {
        uint8_t cmd = tx[0];
        (void)cmd;

        if (rx && rx_len > 0) {
            if (peer->fault == ZSSC_FAULT_FATAL_STATUS) {
                rx[0] = 0xFF;  /* Fatal error status */
            } else if (peer->fault == ZSSC_FAULT_TRUNCATED_RAW) {
                for (uint16_t i = 0; i < rx_len; i++) {
                    rx[i] = (i < 2) ? 0xAA : 0x00;  /* Only first 2 bytes valid */
                }
            } else {
                rx[0] = 0x00; /* Normal completion */
                if (rx_len > 1) rx[1] = 0xAA;
                if (rx_len > 2) rx[2] = 0x55;
            }
        }
    }

    if (peer->fault == ZSSC_FAULT_DUPLICATE_EOC) {
        peer->eoc_active = true;
    }

    return true;
}
