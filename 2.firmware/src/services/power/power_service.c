#include "power_service.h"
#include "power_converter.h"
#include <string.h>

bool power_config_is_valid(const PowerConfig *config)
{
    if (!config || config->vref_mv == 0u || config->divider_ratio == 0u
        || config->sample_period_s == 0u || config->stale_count_max == 0u)
        return false;
    if (config->critical_threshold_mv >= config->low_threshold_mv)
        return false;
    if ((uint32_t)config->low_threshold_mv + (uint32_t)config->hysteresis_mv
        > UINT16_MAX)
        return false;
    return true;
}

PortStatus power_service_init(PowerService *svc, const PowerConfig *config)
{
    if (!svc || !power_config_is_valid(config))
        return PORT_STATUS_INVALID_PARAM;
    memset(svc, 0, sizeof(*svc));
    svc->config = *config;
    svc->current.health = POWER_STATE_UNKNOWN;
    svc->current.available = false;
    return PORT_OK;
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
        if ((uint32_t)mv > (uint32_t)cfg->low_threshold_mv
                            + (uint32_t)cfg->hysteresis_mv)
            return POWER_STATE_NORMAL;
        return POWER_STATE_LOW;

    case POWER_STATE_CRITICAL:
        if ((uint32_t)mv > (uint32_t)cfg->critical_threshold_mv
                            + (uint32_t)cfg->hysteresis_mv)
            return POWER_STATE_LOW;
        return POWER_STATE_CRITICAL;
    }
    return POWER_STATE_UNKNOWN;
}

PortStatus power_service_sample(PowerService *svc, uint16_t raw_adc)
{
    return power_service_sample_at(svc, raw_adc, 0u);
}

PortStatus power_service_sample_at(PowerService *svc,
                                   uint16_t raw_adc,
                                   uint64_t sample_monotonic_us)
{
    if (!svc) return PORT_STATUS_INVALID_PARAM;

    int32_t mv = power_adc_to_mv(raw_adc, svc->config.vref_mv, svc->config.divider_ratio);
    if (mv < 0) return PORT_STATUS_INVALID_PARAM;

    svc->current.raw_adc = raw_adc;
    svc->current.battery_mv = (uint16_t)mv;
    svc->current.sample_sequence = ++svc->sample_sequence;
    svc->current.sample_monotonic_us = sample_monotonic_us;
    svc->current.available = true;
    svc->current.quality_flags = 0;
    svc->stale_count = 0u;

    PowerHealth new_health = determine_health(svc->current.battery_mv,
                                              svc->current.health,
                                              &svc->config);
    svc->current.health = new_health;

    return PORT_OK;
}

bool power_service_mark_read_failure(PowerService *svc)
{
    if (!svc)
        return false;
    if (svc->stale_count < UINT8_MAX)
        svc->stale_count++;
    if (svc->stale_count < svc->config.stale_count_max)
        return false;

    bool changed = svc->current.available
        || svc->current.health != POWER_STATE_UNKNOWN
        || svc->current.quality_flags
            != (POWER_QUALITY_ADC_FAULT | POWER_QUALITY_STALE);
    svc->current.available = false;
    svc->current.health = POWER_STATE_UNKNOWN;
    svc->current.quality_flags = POWER_QUALITY_ADC_FAULT | POWER_QUALITY_STALE;
    return changed;
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
