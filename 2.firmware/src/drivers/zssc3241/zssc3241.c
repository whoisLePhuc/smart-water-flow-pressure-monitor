#include "zssc3241.h"
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
    driver->raw_mailbox_valid = false;
}

bool zssc3241_prepare_start(Zssc3241Driver *driver,
                            uint32_t correlation_id,
                            uint32_t transaction_id,
                            const uint8_t **tx, uint16_t *tx_length)
{
    if (!driver || !tx || !tx_length || driver->state != ZSSC_STATE_STARTING ||
        correlation_id == 0u || transaction_id == 0u)
        return false;
    driver->active_correlation_id = correlation_id;
    driver->active_transaction_id = transaction_id;
    driver->sample_sequence++;
    driver->command_buffer[0] = ZSSC3241_CMD_FULL_MEASURE;
    *tx = driver->command_buffer;
    *tx_length = 1u;
    return true;
}

void zssc3241_on_eoc_asserted(Zssc3241Driver *driver, uint64_t now_us)
{
    if (!driver) return;
    (void)now_us;
    driver->eoc_received_count++;
    driver->state = ZSSC_STATE_READING;
}

bool zssc3241_prepare_read(Zssc3241Driver *driver,
                           uint8_t **rx, uint16_t *rx_length)
{
    if (!driver || !rx || !rx_length ||
        (driver->state != ZSSC_STATE_READING &&
         driver->state != ZSSC_STATE_POLLING))
        return false;
    memset(driver->response_buffer, 0, sizeof(driver->response_buffer));
    *rx = driver->response_buffer;
    *rx_length = ZSSC3241_FULL_RESPONSE_SIZE;
    return true;
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

    if (correlation_id != driver->active_correlation_id) {
        driver->error_count++;
        return;
    }

    if (!success) {
        driver->error_count++;
        driver->state = ZSSC_STATE_RECOVERY;
        return;
    }

    if (driver->state == ZSSC_STATE_STARTING) {
        driver->state = ZSSC_STATE_WAITING_EOC;
        return;
    }

    if ((driver->state != ZSSC_STATE_READING &&
         driver->state != ZSSC_STATE_POLLING) ||
        !rx_data || rx_length < ZSSC3241_PRESSURE_RESPONSE_SIZE) {
        driver->error_count++;
        driver->state = ZSSC_STATE_RECOVERY;
        return;
    }

    uint8_t status = rx_data[0];
    if ((status & ZSSC3241_STATUS_BUSY) != 0u) {
        driver->state = ZSSC_STATE_POLLING;
        return;
    }

    driver->raw_mailbox.status = status;
    driver->raw_mailbox.raw_u24 = ((uint32_t)rx_data[1] << 16)
                                | ((uint32_t)rx_data[2] << 8)
                                | (uint32_t)rx_data[3];
    memset(&driver->raw_mailbox.meta, 0, sizeof(driver->raw_mailbox.meta));
    driver->raw_mailbox.meta.source_generation = driver->generation;
    driver->raw_mailbox.meta.sample_sequence = driver->sample_sequence;
    driver->raw_mailbox.meta.result_version = driver->raw_ready_count + 1u;
    driver->raw_mailbox.meta.sample_monotonic_us = driver->sample_monotonic_us;
    driver->raw_mailbox.meta.completion_monotonic_us = now_us;
    driver->raw_mailbox.meta.validity =
        zssc3241_status_is_production_usable(status) ? DATA_VALID : DATA_INVALID;
    driver->raw_mailbox.meta.freshness = DATA_FRESH;
    driver->raw_mailbox.meta.acceptance =
        zssc3241_status_is_production_usable(status)
            ? DATA_ACCEPTED : DATA_REJECTED;
    driver->raw_mailbox.meta.purpose = MEAS_PURPOSE_PRODUCTION;
    driver->raw_mailbox.meta.origin = DATA_ORIGIN_LIVE_DEVICE;
    driver->raw_mailbox.meta.provenance = PROVENANCE_MEASURED;
    driver->raw_mailbox_valid = true;

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
    driver->raw_mailbox_valid = false;
    driver->state = ZSSC_STATE_TIMEOUT;
}

bool zssc3241_take_raw_pressure(Zssc3241Driver *driver,
                                ZsscRawPressureSample *sample_out)
{
    if (!driver || !sample_out || !driver->raw_mailbox_valid)
        return false;
    *sample_out = driver->raw_mailbox;
    driver->raw_mailbox_valid = false;
    driver->state = ZSSC_STATE_IDLE;
    return true;
}

bool zssc3241_status_is_production_usable(uint8_t status)
{
    const uint8_t faults = ZSSC3241_STATUS_BUSY |
                            ZSSC3241_STATUS_MEMORY_ERROR |
                            ZSSC3241_STATUS_CONNECTION_FAULT |
                            ZSSC3241_STATUS_MATH_SATURATION;
    return (status & 0x80u) == 0u &&
           (status & ZSSC3241_STATUS_POWERED) != 0u &&
           (status & faults) == 0u &&
           (status & ZSSC3241_STATUS_MODE_MASK) !=
               ZSSC3241_STATUS_MODE_RESERVED;
}
