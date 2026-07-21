#include "drivers/zssc3241/zssc3241.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    AppEventQueue queue;
    AppEventQueueConfig config = {
        APP_EVENT_QUEUE_DEFAULT_CAPACITY,
        APP_EVENT_QUEUE_DEFAULT_RESERVED_CRITICAL,
        APP_EVENT_QUEUE_DEFAULT_RESERVED_MEASUREMENT
    };
    app_event_queue_init(&queue, &config);
    Zssc3241Driver driver;
    zssc3241_init(&driver, &queue);

    zssc3241_on_sample_due(&driver, 100u);
    const uint8_t *tx = NULL;
    uint16_t tx_length = 0u;
    assert(zssc3241_prepare_start(&driver, 7u, &tx, &tx_length));
    assert(tx_length == 1u && tx[0] == ZSSC3241_CMD_FULL_MEASURE);
    zssc3241_on_i2c_completion(&driver, 7u, true, NULL, 0u, 120u);
    assert(driver.state == ZSSC_STATE_WAITING_EOC);

    zssc3241_on_eoc_asserted(&driver, 200u);
    uint8_t *rx = NULL;
    uint16_t rx_length = 0u;
    assert(zssc3241_prepare_read(&driver, &rx, &rx_length));
    assert(rx_length == ZSSC3241_FULL_RESPONSE_SIZE);
    rx[0] = 0x40u;
    rx[1] = 0x12u;
    rx[2] = 0x34u;
    rx[3] = 0x56u;
    zssc3241_on_i2c_completion(&driver, 7u, true, rx, rx_length, 230u);

    ZsscRawPressureSample sample;
    assert(zssc3241_take_raw_pressure(&driver, &sample));
    assert(sample.raw_u24 == 0x123456u);
    assert(sample.meta.sample_sequence == 1u);
    assert(sample.meta.sample_monotonic_us == 100u);
    assert(sample.meta.completion_monotonic_us == 230u);
    assert(sample.meta.validity == DATA_VALID);
    assert(!zssc3241_take_raw_pressure(&driver, &sample));
    assert(!zssc3241_status_is_production_usable(ZSSC3241_STATUS_BUSY));

    AppEvent event;
    assert(app_event_queue_try_get(&queue, &event));
    assert(event.id == EVT_PRESSURE_RAW_READY);
    puts("ZSSC3241 Driver Tests: PASS");
    return 0;
}
