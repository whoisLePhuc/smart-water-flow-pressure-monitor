#ifndef SWFPM_POWER_SERVICE_H
#define SWFPM_POWER_SERVICE_H

#include <stdint.h>
#include "domain/power/power_types.h"
#include "domain/power/power_config.h"
#include "ports/port_status.h"

typedef struct {
    PowerConfig    config;
    PowerSnapshot  current;
    uint8_t        stale_count;
    uint64_t       sample_sequence;
} PowerService;

bool    power_config_is_valid(const PowerConfig *config);
PortStatus power_service_init(PowerService *svc, const PowerConfig *config);
PortStatus power_service_sample(PowerService *svc, uint16_t raw_adc);
PortStatus power_service_sample_at(PowerService *svc,
                                   uint16_t raw_adc,
                                   uint64_t sample_monotonic_us);
bool power_service_mark_read_failure(PowerService *svc);
PowerHealth power_service_get_health(const PowerService *svc);
uint16_t  power_service_get_mv(const PowerService *svc);

#endif
