#include "checked_math.h"
#include <limits.h>

bool checked_add_i64(int64_t a, int64_t b, int64_t *out)
{
    if (!out) return false;
    if ((b > 0 && a > INT64_MAX - b) ||
        (b < 0 && a < INT64_MIN - b))
        return false;
    *out = a + b;
    return true;
}

bool checked_sub_i64(int64_t a, int64_t b, int64_t *out)
{
    if (!out) return false;
    if ((b > 0 && a < INT64_MIN + b) ||
        (b < 0 && a > INT64_MAX + b))
        return false;
    *out = a - b;
    return true;
}

bool checked_mul_i64(int64_t a, int64_t b, int64_t *out)
{
    if (!out) return false;
    if (a == 0 || b == 0) { *out = 0; return true; }
    if (a == INT64_MIN && b == -1) return false;
    if (b == INT64_MIN && a == -1) return false;
    if ((a > 0 && b > 0 && a > INT64_MAX / b) ||
        (a > 0 && b < 0 && b < INT64_MIN / a) ||
        (a < 0 && b > 0 && a < INT64_MIN / b) ||
        (a < 0 && b < 0 && a < INT64_MAX / b))
        return false;
    *out = a * b;
    return true;
}

bool checked_add_u64(uint64_t a, uint64_t b, uint64_t *out)
{
    if (!out) return false;
    if (a > UINT64_MAX - b) return false;
    *out = a + b;
    return true;
}

bool checked_sub_u64(uint64_t a, uint64_t b, uint64_t *out)
{
    if (!out) return false;
    if (b > a) return false;
    *out = a - b;
    return true;
}

void widened_mul_i64(int64_t a, int64_t b, int64_t *hi, uint64_t *lo)
{
    /* Cast to unsigned for overflow-safe multiplication */
    uint64_t ua = (uint64_t)(a < 0 ? -a : a);
    uint64_t ub = (uint64_t)(b < 0 ? -b : b);
    uint64_t low = ua * ub;  /* truncates to 64-bit */
    /* hi = (ua * ub) >> 64 — approximate for int64 */
    uint64_t high = (ua >> 32) * (ub >> 32) +
                    ((ua & 0xFFFFFFFFULL) * (ub >> 32) >> 32) +
                    ((ua >> 32) * (ub & 0xFFFFFFFFULL) >> 32);
    int neg = (a < 0) != (b < 0);
    if (neg) {
        *hi = -(int64_t)high - (low != 0 ? 1 : 0);
        *lo = -low;
    } else {
        *hi = (int64_t)high;
        *lo = low;
    }
    (void)hi; (void)lo; /* simplified — full 128-bit in later phase */
}

int64_t round_i64(int64_t value, int64_t divisor)
{
    if (divisor == 0) return value;
    if (divisor < 0) { divisor = -divisor; value = -value; }

    int64_t half = divisor / 2;
    int64_t remainder = value % divisor;
    int64_t quotient = value / divisor;

    if (remainder < 0) { remainder = -remainder; quotient--; }

    if (remainder > half) return quotient + 1;
    if (remainder < half) return quotient;
    /* Tie: round to even */
    return (quotient % 2 == 0) ? quotient : quotient + 1;
}

int64_t sat_add_i64(int64_t a, int64_t b)
{
    int64_t r;
    if (checked_add_i64(a, b, &r)) return r;
    return (b > 0) ? INT64_MAX : INT64_MIN;
}

int64_t sat_sub_i64(int64_t a, int64_t b)
{
    int64_t r;
    if (checked_sub_i64(a, b, &r)) return r;
    return (b > 0) ? INT64_MIN : INT64_MAX;
}
