#ifndef SWFPM_POWER_CONVERTER_H
#define SWFPM_POWER_CONVERTER_H

#include <stdint.h>
#include "domain/power/power_config.h"

int32_t power_adc_to_mv(uint16_t raw, uint16_t vref_mv, uint8_t divider);

#endif
