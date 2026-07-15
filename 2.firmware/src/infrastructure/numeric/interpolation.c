#include "interpolation.h"
#include "checked_math.h"

bool table_is_monotonic(const int64_t *x, const int64_t *y, uint16_t size)
{
    if (!x || !y || size < 2) return false;
    for (uint16_t i = 1; i < size; i++) {
        if (x[i] <= x[i-1]) return false;
        if (y[i] <= y[i-1]) return false;
    }
    return true;
}

bool interpolate_i64(int64_t x,
                     const int64_t *x_table, const int64_t *y_table,
                     uint16_t size,
                     int64_t *y_out)
{
    if (!x_table || !y_table || !y_out || size < 2) return false;

    /* Clamp below */
    if (x <= x_table[0]) { *y_out = y_table[0]; return true; }
    /* Clamp above */
    if (x >= x_table[size-1]) { *y_out = y_table[size-1]; return true; }

    /* Binary search for segment */
    uint16_t lo = 0, hi = size - 1;
    while (hi - lo > 1) {
        uint16_t mid = (lo + hi) / 2;
        if (x < x_table[mid])
            hi = mid;
        else
            lo = mid;
    }

    int64_t x0 = x_table[lo], x1 = x_table[hi];
    int64_t y0 = y_table[lo], y1 = y_table[hi];
    int64_t dx = x1 - x0;
    if (dx <= 0) return false;

    /* y = y0 + (x - x0) * (y1 - y0) / dx */
    int64_t x_offset, y_range, num;
    if (!checked_sub_i64(x, x0, &x_offset)) return false;
    if (!checked_sub_i64(y1, y0, &y_range)) return false;
    if (!checked_mul_i64(x_offset, y_range, &num)) return false;

    *y_out = y0 + round_i64(num, dx);
    return true;
}

bool apply_gain_offset(int64_t x, int64_t gain, int64_t offset,
                       uint8_t shift, int64_t *y_out)
{
    if (!y_out) return false;
    int64_t prod;
    if (!checked_mul_i64(x, gain, &prod)) return false;
    if (!checked_add_i64(prod, offset, &prod)) return false;
    *y_out = (shift == 0) ? prod : round_i64(prod, (int64_t)1 << shift);
    return true;
}
