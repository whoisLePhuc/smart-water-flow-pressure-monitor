#include "power_facade.h"
#include "infrastructure/repositories/repo_transaction.h"
#include <string.h>

static PortStatus publish_power_snapshot(PowerFacade *f, PowerHealth previous_health)
{
    RepoWriteTxn txn;
    txn_init(&txn);
    if (!txn_begin(&txn, f->repo))
        return PORT_STATUS_BUSY;
    if (!txn_write_power(&txn, &f->svc.current) || !txn_commit(&txn)) {
        txn_abort(&txn);
        return PORT_STATUS_HARDWARE_ERROR;
    }

    if (f->svc.current.health != previous_health) {
        AppEvent event;
        memset(&event, 0, sizeof(event));
        event.id = EVT_POWER_STATUS_CHANGED;
        event.priority = EVENT_PRIO_BACKGROUND;
        event.delivery = DELIVERY_LEVEL;
        event.monotonic_timestamp_us = f->svc.current.sample_monotonic_us;
        event.payload[0] = (uint8_t)f->svc.current.health;
        event.payload_size = 1u;
        EventPostResult post_result = app_event_queue_post(f->event_queue, &event);
        if (post_result != EVENT_POST_OK && post_result != EVENT_POST_COALESCED)
            return PORT_STATUS_BUSY;
    }
    return PORT_OK;
}

PortStatus power_facade_init(PowerFacade *f,
                             const PowerConfig *cfg,
                             const AdcPort *adc_port,
                             DataRepository *repo,
                             AppEventQueue *event_queue)
{
    if (!f || !adc_port || !repo || !event_queue)
        return PORT_STATUS_INVALID_PARAM;
    memset(f, 0, sizeof(*f));
    if (cfg) f->config = *cfg;
    else f->config = (PowerConfig)POWER_CONFIG_DEFAULT;
    if (power_service_init(&f->svc, &f->config) != PORT_OK)
        return PORT_STATUS_INVALID_PARAM;
    f->adc_port = adc_port;
    f->repo = repo;
    f->event_queue = event_queue;
    f->initialized = true;
    return PORT_OK;
}

PortStatus power_facade_sample(PowerFacade *f, uint16_t raw_adc)
{
    if (!f || !f->initialized) return PORT_STATUS_UNAVAILABLE;
    return power_service_sample(&f->svc, raw_adc);
}

PortStatus power_facade_process_sample(PowerFacade *f,
                                       uint64_t sample_monotonic_us)
{
    if (!f || !f->initialized)
        return PORT_STATUS_UNAVAILABLE;

    uint16_t raw_adc = 0u;
    PowerHealth previous_health = f->svc.current.health;
    PortStatus read_status = adc_port_read(
        f->adc_port, ADC_CHANNEL_BATTERY, &raw_adc);
    if (read_status != PORT_OK) {
        if (power_service_mark_read_failure(&f->svc))
            (void)publish_power_snapshot(f, previous_health);
        return read_status;
    }

    PortStatus sample_status = power_service_sample_at(
        &f->svc, raw_adc, sample_monotonic_us);
    if (sample_status != PORT_OK)
        return sample_status;
    return publish_power_snapshot(f, previous_health);
}

PortStatus power_facade_get_status(const PowerFacade *f)
{
    if (!f || !f->initialized) return PORT_STATUS_UNAVAILABLE;
    return PORT_OK;
}

PowerHealth power_facade_get_health(const PowerFacade *f)
{
    if (!f || !f->initialized) return POWER_STATE_UNKNOWN;
    return power_service_get_health(&f->svc);
}

uint16_t power_facade_get_mv(const PowerFacade *f)
{
    if (!f || !f->initialized) return 0xFFFF;
    return power_service_get_mv(&f->svc);
}
