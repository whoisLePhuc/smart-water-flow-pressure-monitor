#include "platform/peers/peer_max35103.h"
#include <string.h>

void max_peer_init(Max35103Peer *peer)
{
    memset(peer, 0, sizeof(*peer));
    peer->state = MAX_PEER_IDLE;
    peer->generation = 1;
    peer->cycle_latency_us = 40000;  /* 40 ms typical */
    peer->spi_latency_us = 50;       /* 50 us per SPI op */
    peer->fault = MAX_FAULT_NONE;
}

void max_peer_reset(Max35103Peer *peer, bool unexpected)
{
    if (!peer) return;
    peer->state = MAX_PEER_RESET;
    peer->generation++;
    peer->int_active = false;
    if (!unexpected) {
        peer->state = MAX_PEER_IDLE;
    }
}

void max_peer_configure(Max35103Peer *peer)
{
    if (!peer) return;
    peer->state = MAX_PEER_CONFIGURED;
}

uint64_t max_peer_schedule_cycle(Max35103Peer *peer, uint64_t start_us)
{
    if (!peer) return 0;

    peer->state = MAX_PEER_CYCLE_PENDING;
    peer->cycle_count++;

    /* Check for fault: missing INT */
    if (peer->fault == MAX_FAULT_MISSING_INT) {
        peer->int_active = false;
        return start_us + peer->cycle_latency_us; /* No INT scheduled */
    }

    /* Normal: INT asserted at start_us + latency */
    peer->int_active = true;
    peer->int_assert_count++;
    return start_us + 100;  /* INT asserts after 100 us */
}

void max_peer_clear_int(Max35103Peer *peer)
{
    if (!peer) return;
    peer->int_active = false;
}

bool max_peer_is_int_active(const Max35103Peer *peer)
{
    return peer ? peer->int_active : false;
}

void max_peer_set_fault(Max35103Peer *peer, MaxPeerFault fault)
{
    if (!peer) return;
    peer->fault = fault;
}

bool max_peer_plan_spi(void *context,
                        const uint8_t *tx, uint8_t *rx,
                        uint16_t length,
                        uint64_t *latency_us,
                        uint32_t *status_flags)
{
    Max35103Peer *peer = (Max35103Peer *)context;
    if (!peer || !latency_us || !status_flags) return false;

    /* Check for no-completion fault */
    if (peer->fault == MAX_FAULT_NO_COMPLETION) {
        return false;  /* SPI rejected */
    }

    peer->spi_operations++;
    *latency_us = peer->spi_latency_us;
    *status_flags = 0;

    /* Simulate register read response */
    if (tx && length > 0) {
        uint8_t cmd = tx[0];
        if ((cmd & 0x80) == 0) {
            /* Register read */
            peer->register_reads++;
            if (rx && length >= 2) {
                if (peer->fault == MAX_FAULT_INVALID_RESULT) {
                    rx[1] = 0xFF;  /* Invalid data */
                } else if (peer->fault == MAX_FAULT_PARTIAL_RESULT) {
                    /* Only return partial data — truncated */
                    for (uint16_t i = 1; i < length; i++) {
                        rx[i] = (i < length / 2) ? 0xAA : 0x00;
                    }
                } else {
                    rx[1] = 0xAA;  /* Normal status */
                }
            }
        } else {
            /* Command write */
            if (cmd == 0xFF) {
                peer->unsupported_cmds++;
                *status_flags = 0x01;  /* Unsupported command */
                return false;
            }
        }
    }

    /* Check for duplicate INT fault — fire INT again */
    if (peer->fault == MAX_FAULT_DUPLICATE_INT) {
        peer->int_active = true;
    }

    return true;
}
