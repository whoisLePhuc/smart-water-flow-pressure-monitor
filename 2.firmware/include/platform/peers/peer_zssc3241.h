#ifndef SWFPM_PEER_ZSSC3241_H
#define SWFPM_PEER_ZSSC3241_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    ZSSC_PEER_SLEEP,
    ZSSC_PEER_IDLE,
    ZSSC_PEER_CONVERTING,
    ZSSC_PEER_RESULT_READY,
    ZSSC_PEER_FAULT
} ZsscPeerState;

typedef enum {
    ZSSC_FAULT_NONE,
    ZSSC_FAULT_NO_COMPLETION,
    ZSSC_FAULT_MISSING_EOC,
    ZSSC_FAULT_DUPLICATE_EOC,
    ZSSC_FAULT_STUCK_BUSY,
    ZSSC_FAULT_FATAL_STATUS,
    ZSSC_FAULT_TRUNCATED_RAW,
    ZSSC_FAULT_UNEXPECTED_RESET,
} ZsscPeerFault;

typedef struct {
    ZsscPeerState state;
    ZsscPeerFault fault;
    uint32_t      generation;
    uint64_t      conversion_count;

    uint64_t      conversion_latency_us;  /* Time from start to EOC */
    uint64_t      i2c_latency_us;         /* Per I2C operation latency */

    bool          eoc_active;
    uint32_t      i2c_operations;
    uint32_t      unsupported_cmds;
} Zssc3241Peer;

void zssc_peer_init(Zssc3241Peer *peer);
void zssc_peer_reset(Zssc3241Peer *peer, bool unexpected);

bool zssc_peer_plan_i2c(void *context, uint8_t slave_addr,
                         const uint8_t *tx, uint16_t tx_len,
                         uint8_t *rx, uint16_t rx_len,
                         uint64_t *latency_us, uint32_t *status_flags);

/* Schedule EOC assertion after conversion */
uint64_t zssc_peer_schedule_eoc(Zssc3241Peer *peer, uint64_t start_us);

void zssc_peer_clear_eoc(Zssc3241Peer *peer);
bool zssc_peer_is_eoc_active(const Zssc3241Peer *peer);
void zssc_peer_set_fault(Zssc3241Peer *peer, ZsscPeerFault fault);

#endif
