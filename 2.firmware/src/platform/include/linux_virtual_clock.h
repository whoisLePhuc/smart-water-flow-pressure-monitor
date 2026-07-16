#ifndef SWFPM_LINUX_VIRTUAL_CLOCK_H
#define SWFPM_LINUX_VIRTUAL_CLOCK_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Linux Virtual Clock — deterministic monotonic time source.
 *
 * Two modes:
 *   DETERMINISTIC — time only advances via AdvanceBy/AdvanceTo.
 *                   Used for all automated tests.
 *   REALTIME      — wraps clock_gettime(CLOCK_MONOTONIC).
 *                   Used for interactive simulation.
 *
 * Wall clock is an independent domain: wall-clock steps do not
 * modify monotonic now_us.
 */

typedef enum {
    LINUX_CLOCK_MODE_DETERMINISTIC,
    LINUX_CLOCK_MODE_REALTIME
} LinuxClockMode;

typedef struct {
    /* Monotonic domain */
    uint64_t now_us;              /* Current virtual monotonic time */
    uint64_t boot_origin_us;      /* Monotonic time at last reset */
    uint32_t boot_generation;     /* Incremented on each reset */

    /* Wall-clock domain (independent) */
    int64_t  wall_base_s;         /* Wall clock Unix seconds */
    uint32_t wall_subsecond_us;
    uint32_t time_generation;
    bool     wall_time_valid;

    /* Mode */
    LinuxClockMode mode;
} LinuxVirtualClock;


void linux_clock_init(LinuxVirtualClock *clock, LinuxClockMode mode);

/* Reset creates a new boot generation and resets monotonic origin to 0.
 * Wall clock is optionally preserved per policy. */
void linux_clock_reset(LinuxVirtualClock *clock, bool preserve_wall);


/* Current monotonic time. In DETERMINISTIC mode, returns now_us.
 * In REALTIME mode, wraps clock_gettime. */
uint64_t linux_clock_now_us(const LinuxVirtualClock *clock);

/* Advance by delta. Must not overflow uint64_t. In REALTIME mode, no-op. */
void linux_clock_advance_by(LinuxVirtualClock *clock, uint64_t delta_us);

/* Advance to absolute time. Must not go backward (target < now).
 * In REALTIME mode, no-op. Returns true if advanced, false if rejected. */
bool linux_clock_advance_to(LinuxVirtualClock *clock, uint64_t target_us);


/* Set wall time. Does not modify monotonic now_us.
 * Increments time_generation. */
void linux_clock_set_wall(LinuxVirtualClock *clock,
                          int64_t wall_s, uint32_t subsecond_us,
                          bool valid);

#endif /* SWFPM_LINUX_VIRTUAL_CLOCK_H */
