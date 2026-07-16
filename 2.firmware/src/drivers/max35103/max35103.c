#include "max35103.h"
#include <string.h>

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static bool absolute_q16_to_ps(uint16_t integer, uint16_t fraction,
                               int64_t *picoseconds)
{
    if (!picoseconds || integer > 0x7FFFu)
        return false;
    uint64_t q16 = ((uint64_t)integer << 16) | fraction;
    *picoseconds = (int64_t)((q16 * UINT64_C(250000) + UINT64_C(32768)) /
                             UINT64_C(65536));
    return true;
}

static int64_t signed_q16_to_ps(uint16_t integer, uint16_t fraction)
{
    uint32_t joined = ((uint32_t)integer << 16) | fraction;
    int64_t signed_q16 = (joined & UINT32_C(0x80000000)) != 0u
        ? (int64_t)joined - INT64_C(0x100000000)
        : (int64_t)joined;
    return signed_q16 * INT64_C(250000) / INT64_C(65536);
}

bool max35103_decode_flow_frame(const uint8_t *frame, uint16_t length,
                                Max35103RawFlowSample *sample_out)
{
    if (!frame || !sample_out || length < MAX35103_FLOW_FRAME_SIZE)
        return false;
    memset(sample_out, 0, sizeof(*sample_out));
    uint16_t up_integer = be16(frame + 0);
    uint16_t up_fraction = be16(frame + 2);
    uint16_t down_integer = be16(frame + 4);
    uint16_t down_fraction = be16(frame + 6);
    uint16_t diff_integer = be16(frame + 8);
    uint16_t diff_fraction = be16(frame + 10);
    uint16_t cycle = be16(frame + 12);
    if (!absolute_q16_to_ps(up_integer, up_fraction, &sample_out->tof_up_ps) ||
        !absolute_q16_to_ps(down_integer, down_fraction,
                            &sample_out->tof_down_ps))
        return false;
    sample_out->tof_diff_ps = signed_q16_to_ps(diff_integer, diff_fraction);
    /* MAX35103 defines TOF_DIFF as AVGUP - AVGDN. Reject torn/misaligned
     * multi-register reads instead of publishing inconsistent data. */
    int64_t expected_diff = sample_out->tof_up_ps - sample_out->tof_down_ps;
    int64_t coherence_error = sample_out->tof_diff_ps - expected_diff;
    if (coherence_error < 0) coherence_error = -coherence_error;
    if (coherence_error > 4) return false;
    sample_out->tof_range = (uint8_t)(cycle >> 8);
    sample_out->valid_cycle_count = (uint8_t)cycle;
    return sample_out->valid_cycle_count > 0u;
}

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
    driver->sample_sequence++;
    driver->raw_flow_mailbox_valid = false;
}

void max35103_on_spi_completion(Max35103Driver *driver,
                                 uint32_t correlation_id,
                                 bool success,
                                 const uint8_t *rx_data,
                                 uint16_t rx_length,
                                 uint64_t now_us)
{
    if (!driver) return;
    if (!success) {
        driver->error_count++;
        driver->state = MAX_STATE_RECOVERY;
        return;
    }

    driver->spi_completion_count++;
    driver->state = MAX_STATE_VALIDATING;

    Max35103RawFlowSample decoded;
    if (!max35103_decode_flow_frame(rx_data, rx_length, &decoded)) {
        driver->error_count++;
        driver->state = MAX_STATE_RECOVERY;
        return;
    }
    decoded.meta.source_generation = driver->generation;
    decoded.meta.sample_sequence = driver->sample_sequence;
    decoded.meta.result_version = driver->raw_ready_count + 1u;
    decoded.meta.sample_monotonic_us = driver->sample_monotonic_us;
    decoded.meta.completion_monotonic_us = now_us;
    decoded.meta.validity = DATA_VALID;
    decoded.meta.freshness = DATA_FRESH;
    decoded.meta.acceptance = DATA_ACCEPTED;
    decoded.meta.purpose = MEAS_PURPOSE_PRODUCTION;
    decoded.meta.origin = DATA_ORIGIN_LIVE_DEVICE;
    decoded.meta.provenance = PROVENANCE_MEASURED;
    driver->active_correlation_id = correlation_id;
    driver->raw_flow_mailbox = decoded;
    driver->raw_flow_mailbox_valid = true;

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
    driver->raw_flow_mailbox_valid = false;
    driver->state = MAX_STATE_TIMEOUT;
}

bool max35103_take_raw_flow(Max35103Driver *driver,
                            Max35103RawFlowSample *sample_out)
{
    if (!driver || !sample_out || !driver->raw_flow_mailbox_valid)
        return false;
    *sample_out = driver->raw_flow_mailbox;
    driver->raw_flow_mailbox_valid = false;
    driver->state = MAX_STATE_IDLE;
    return true;
}
