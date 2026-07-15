#ifndef SWFPM_INTERPOLATION_H
#define SWFPM_INTERPOLATION_H

#include <stdint.h>
#include <stdbool.h>

/* Linear interpolation helpers for processing tables. */

/* Check if a table of (x, y) pairs is strictly monotonic increasing.
 * size must be >= 2. */
bool table_is_monotonic(const int64_t *x, const int64_t *y, uint16_t size);

/* Linear interpolate y at x, using strictly monotonic table (x[], y[], size).
 * If exact_x is found, returns exact_y. Clamps for out-of-range.
 * Returns false if table invalid. */
bool interpolate_i64(int64_t x,
                     const int64_t *x_table, const int64_t *y_table,
                     uint16_t size,
                     int64_t *y_out);

/* Fixed-point gain/offset: y = (x * gain + offset) >> shift */
bool apply_gain_offset(int64_t x, int64_t gain, int64_t offset,
                       uint8_t shift, int64_t *y_out);

#endif
