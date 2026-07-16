#include "power_service.h"
#include "power_converter.h"
#include <string.h>

void power_service_init(PowerService *svc, const PowerConfig *config)
{
    memset(svc, 0, sizeof(*svc));
    if (config) svc->config = *config;
    svc->current.health = POWER_STATE_UNKNOWN;
    svc->current.available = false;
}

static PowerHealth determine_health(uint16_t mv, PowerHealth current, const PowerConfig *cfg)
{
    switch (current) {
    case POWER_STATE_UNKNOWN:
    case POWER_STATE_NORMAL:
        if (mv < cfg->critical_threshold_mv) return POWER_STATE_CRITICAL;
        if (mv < cfg->low_threshold_mv)      return POWER_STATE_LOW;
        return POWER_STATE_NORMAL;

    case POWER_STATE_LOW:
        if (mv < cfg->critical_threshold_mv) return POWER_STATE_CRITICAL;
        if (mv > cfg->low_threshold_mv + cfg->hysteresis_mv) return POWER_STATE_NORMAL;
        return POWER_STATE_LOW;

    case POWER_STATE_CRITICAL:
        if (mv > cfg->critical_threshold_mv + cfg->hysteresis_mv) return POWER_STATE_LOW;
        return POWER_STATE_CRITICAL;
    }
    return POWER_STATE_UNKNOWN;
}

PortStatus power_service_sample(PowerService *svc, uint16_t raw_adc)
{
    if (!svc) return PORT_STATUS_INVALID_PARAM;

    int32_t mv = power_adc_to_mv(raw_adc, svc->config.vref_mv, svc->config.divider_ratio);
    if (mv < 0) return PORT_STATUS_INVALID_PARAM;

    svc->current.raw_adc = raw_adc;
    svc->current.battery_mv = (uint16_t)mv;
    svc->current.sample_sequence = ++svc->sample_sequence;
    svc->current.available = true;
    svc->current.quality_flags = 0;

    PowerHealth new_health = determine_health(svc->current.battery_mv,
                                              svc->current.health,
                                              &svc->config);
    svc->current.health = new_health;

    return PORT_OK;
}

PowerHealth power_service_get_health(const PowerService *svc)
{
    if (!svc) return POWER_STATE_UNKNOWN;
    return svc->current.health;
}

uint16_t power_service_get_mv(const PowerService *svc)
{
    if (!svc || !svc->current.available) return 0xFFFF;
    return svc->current.battery_mv;
}
