#ifndef SWFPM_MONOTONIC_CLOCK_PORT_H
#define SWFPM_MONOTONIC_CLOCK_PORT_H

#include <stdint.h>

/*
 * Monotonic clock port
 *
 * Returns microseconds since an unspecified epoch. Must be monotonic
 * (never decrease within a boot cycle). Used for all timeouts,
 * deadlines, freshness calculation, and event ordering.
 *
 * The platform backend provides the implementation. Portable core
 * code MUST NOT call clock_gettime(), gettimeofday(), or any
 * wall-clock API directly.
 */

uint64_t monotonic_now_us(void);

#endif /* SWFPM_MONOTONIC_CLOCK_PORT_H */
