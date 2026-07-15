#ifndef SWFPM_CHECKED_MATH_H
#define SWFPM_CHECKED_MATH_H

#include <stdint.h>
#include <stdbool.h>

/* Checked arithmetic — overflow-safe operations for fixed-point processing.
 * All return false on overflow; output written via pointer. */

bool checked_add_i64(int64_t a, int64_t b, int64_t *out);
bool checked_sub_i64(int64_t a, int64_t b, int64_t *out);
bool checked_mul_i64(int64_t a, int64_t b, int64_t *out);
bool checked_add_u64(uint64_t a, uint64_t b, uint64_t *out);
bool checked_sub_u64(uint64_t a, uint64_t b, uint64_t *out);

/* Widened multiply: int64 a * int64 b -> int128 (via two int64 outputs: hi, lo) */
void widened_mul_i64(int64_t a, int64_t b, int64_t *hi, uint64_t *lo);

/* Rounding: round-to-nearest, ties to even (banker's rounding) */
int64_t round_i64(int64_t value, int64_t divisor);

/* Saturating arithmetic */
int64_t sat_add_i64(int64_t a, int64_t b);
int64_t sat_sub_i64(int64_t a, int64_t b);

#endif
