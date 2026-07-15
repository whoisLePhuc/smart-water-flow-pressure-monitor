#include "drivers/max35103.h"
#include <string.h>

void max35103_init(Max35103Driver *driver, AppEventQueue *event_queue)
{
    memset(driver, 0, sizeof(*driver));
    driver->generation = 1;
    driver->state = MAX_STATE_IDLE;
    driver->event_queue = event_queue;
    driver->supervision_timeout_us = 50000; /* 50 ms default */
}

void max35103_on_irq(Max35103Driver *driver, uint64_t now_us)
{
    if (!driver) return;
    driver->irq_received_count++;
    driver->state = MAX_STATE_IRQ_RECEIVED;
    driver->sample_monotonic_us = now_us;
}

void max35103_on_spi_completion(Max35103Driver *driver,
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
        driver->state = MAX_STATE_RECOVERY;
        return;
    }

    driver->spi_completion_count++;
    driver->state = MAX_STATE_VALIDATING;

    /* After status/result reads, publish raw-ready event */
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = EVT_MAX_RAW_READY;
    evt.source_id = 0;
    evt.priority = EVENT_PRIO_MEASUREMENT;
    evt.delivery = DELIVERY_MAILBOX;
    evt.correlation_id = driver->active_correlation_id;
    evt.source_generation = driver->generation;
    evt.monotonic_timestamp_us = now_us;
    app_event_queue_post(driver->event_queue, &evt);

    driver->raw_ready_count++;
    driver->state = MAX_STATE_RAW_READY;
}

void max35103_on_timeout(Max35103Driver *driver, uint64_t now_us)
{
    if (!driver) return;
    (void)now_us;
    driver->timeout_count++;
    driver->state = MAX_STATE_TIMEOUT;
}
