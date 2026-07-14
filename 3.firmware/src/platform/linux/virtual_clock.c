#define _POSIX_C_SOURCE 199309L
#include "platform/monotonic_clock_port.h"
#include "virtual_clock.h"
#include <time.h>
#include <stdint.h>

/*
 * Virtual monotonic clock for Linux simulation.
 *
 * Two modes:
 *   VIRTUAL — time only advances when explicitly set via
 *             virtual_clock_set() or virtual_clock_advance().
 *             Used for deterministic tests.
 *   REAL    — wraps clock_gettime(CLOCK_MONOTONIC).
 *             Used for interactive simulation.
 */

/* ClockMode enum: use macros from virtual_clock.h */
static int  current_mode = CLOCK_MODE_REAL;
static uint64_t   virtual_now_us = 0;

void virtual_clock_set_mode(int mode)
{
    current_mode = mode;
}

void virtual_clock_set(uint64_t now_us)
{
    virtual_now_us = now_us;
    current_mode = CLOCK_MODE_VIRTUAL;
}

void virtual_clock_advance(uint64_t delta_us)
{
    virtual_now_us += delta_us;
    current_mode = CLOCK_MODE_VIRTUAL;
}

uint64_t monotonic_now_us(void)
{
    if (current_mode == CLOCK_MODE_VIRTUAL)
        return virtual_now_us;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000);
}
