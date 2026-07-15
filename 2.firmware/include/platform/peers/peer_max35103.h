#ifndef SWFPM_PEER_MAX35103_H
#define SWFPM_PEER_MAX35103_H

#include <stdint.h>
#include <stdbool.h>

/*
 * MAX35103 stateful emulator peer.
 *
 * Simulates the observable subset of MAX35103 behavior:
 *   - reset/init/config
 *   - event-timing cycle
 *   - INT assertion (asserted-until-drained)
 *   - status/result register response
 *   - fault injection (no-completion, missing INT, invalid result)
 *
 * Peer does NOT post EVT_MAX_RAW_READY — only creates GPIO/SPI evidence.
 */

typedef enum {
    MAX_PEER_IDLE,
    MAX_PEER_RESET,
    MAX_PEER_CONFIGURED,
    MAX_PEER_EVENT_TIMING,
    MAX_PEER_CYCLE_PENDING,
    MAX_PEER_RESULT_READY,
    MAX_PEER_FAULT
} MaxPeerState;

typedef enum {
    MAX_FAULT_NONE,
    MAX_FAULT_NO_COMPLETION,      /* SPI accepted but no completion scheduled */
    MAX_FAULT_MISSING_INT,        /* Cycle completes but INT not asserted */
    MAX_FAULT_DUPLICATE_INT,      /* INT fires twice */
    MAX_FAULT_INT_ALREADY_ACTIVE, /* INT already high when armed */
    MAX_FAULT_INVALID_RESULT,     /* Result register has sentinel/invalid data */
    MAX_FAULT_PARTIAL_RESULT,     /* Truncated result on read */
    MAX_FAULT_UNEXPECTED_RESET,   /* Peer resets unexpectedly */
} MaxPeerFault;

typedef struct {
    MaxPeerState state;
    MaxPeerFault fault;
    uint32_t     generation;
    uint64_t     cycle_count;
    uint64_t     int_assert_count;

    /* Configurable timing */
    uint64_t     cycle_latency_us;    /* Time from INT to result ready */
    uint64_t     spi_latency_us;      /* Per SPI operation latency */

    /* INT state */
    bool         int_active;          /* Current INT output level */

    /* Diagnostics */
    uint32_t     spi_operations;
    uint32_t     register_reads;
    uint32_t     unsupported_cmds;
} Max35103Peer;

void max_peer_init(Max35103Peer *peer);

/* Reset the peer. If unexpected=true, simulates an unexpected device reset. */
void max_peer_reset(Max35103Peer *peer, bool unexpected);

/* Configure peer for event-timing mode */
void max_peer_configure(Max35103Peer *peer);

/* Plan an SPI transfer. Called by the SPI provider's peer interface.
 * Returns true if the transfer is valid. Sets latency and status. */
bool max_peer_plan_spi(void *context,
                        const uint8_t *tx, uint8_t *rx,
                        uint16_t length,
                        uint64_t *latency_us,
                        uint32_t *status_flags);

/* Schedule INT assertion for a measurement cycle.
 * Returns the time at which INT will be asserted. */
uint64_t max_peer_schedule_cycle(Max35103Peer *peer, uint64_t start_us);

/* Clear/acknowledge INT */
void max_peer_clear_int(Max35103Peer *peer);

/* Get current INT state */
bool max_peer_is_int_active(const Max35103Peer *peer);

/* Inject a fault for the next cycle */
void max_peer_set_fault(Max35103Peer *peer, MaxPeerFault fault);

#endif
