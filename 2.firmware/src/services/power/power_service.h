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

void    power_service_init(PowerService *svc, const PowerConfig *config);
PortStatus power_service_sample(PowerService *svc, uint16_t raw_adc);
PowerHealth power_service_get_health(const PowerService *svc);
uint16_t  power_service_get_mv(const PowerService *svc);

#endif
