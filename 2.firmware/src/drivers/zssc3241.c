#include "drivers/zssc3241.h"
#include <string.h>

void zssc3241_init(Zssc3241Driver *driver, AppEventQueue *event_queue)
{
    memset(driver, 0, sizeof(*driver));
    driver->generation = 1;
    driver->state = ZSSC_STATE_IDLE;
    driver->event_queue = event_queue;
}

void zssc3241_on_sample_due(Zssc3241Driver *driver, uint64_t now_us)
{
    if (!driver) return;

    if (driver->state != ZSSC_STATE_IDLE && driver->state != ZSSC_STATE_SLEEP) {
        /* Busy — skip, will be logged by scheduler */
        return;
    }

    driver->sample_due_count++;
    driver->state = ZSSC_STATE_STARTING;
    driver->sample_monotonic_us = now_us;
    /* Actual I2C submission handled by MeasurementManager via I2cBusManager */
}

void zssc3241_on_eoc_asserted(Zssc3241Driver *driver, uint64_t now_us)
{
    if (!driver) return;
    (void)now_us;
    driver->eoc_received_count++;
    driver->state = ZSSC_STATE_READING;
}

void zssc3241_on_poll_due(Zssc3241Driver *driver, uint64_t now_us)
{
    if (!driver) return;
    (void)now_us;
    /* Poll fallback: check status via I2C — handled by MeasurementManager */
    driver->state = ZSSC_STATE_POLLING;
}

void zssc3241_on_i2c_completion(Zssc3241Driver *driver,
                                 uint32_t correlation_id,
                                 bool success,
                                 const uint8_t *rx_data,
                                 uint16_t rx_length,
                                 uint64_t now_us)
{
    if (!driver) return;
    (void)correlation_id;
    (void)rx_data;
    (void)rx_length;

    if (!success) {
        driver->error_count++;
        driver->state = ZSSC_STATE_RECOVERY;
        return;
    }

    /* Publish pressure raw-ready event */
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = EVT_PRESSURE_RAW_READY;
    evt.source_id = 0;
    evt.priority = EVENT_PRIO_MEASUREMENT;
    evt.delivery = DELIVERY_MAILBOX;
    evt.correlation_id = driver->active_correlation_id;
    evt.source_generation = driver->generation;
    evt.monotonic_timestamp_us = now_us;
    app_event_queue_post(driver->event_queue, &evt);

    driver->raw_ready_count++;
    driver->state = ZSSC_STATE_RAW_READY;
}

void zssc3241_on_timeout(Zssc3241Driver *driver, uint64_t now_us)
{
    if (!driver) return;
    (void)now_us;
    driver->timeout_count++;
    driver->state = ZSSC_STATE_TIMEOUT;
}
