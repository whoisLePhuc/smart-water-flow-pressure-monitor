#include "power_converter.h"

/* ADC → battery terminal mV using integer arithmetic.
 * Formula: battery_mv = (raw * vref * divider) / 4096
 * Rounding: add 2048 (4096/2) to numerator before division. */

int32_t power_adc_to_mv(uint16_t raw, uint16_t vref_mv, uint8_t divider)
{
    if (divider == 0) return -1;
    uint64_t num = (uint64_t)raw * vref_mv * divider;
    return (int32_t)((num + 2048) / 4096);
}
