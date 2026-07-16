#include "platform/stm32/adapters/adc_port_stm32.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    uint32_t configured_channel;
    uint32_t value;
    Stm32AdcHalStatus poll_status;
    unsigned stop_count;
} FakeHal;

static Stm32AdcHalStatus configure(void *ctx, uint32_t channel)
{ ((FakeHal *)ctx)->configured_channel = channel; return STM32_ADC_HAL_OK; }
static Stm32AdcHalStatus start(void *ctx)
{ (void)ctx; return STM32_ADC_HAL_OK; }
static Stm32AdcHalStatus poll(void *ctx, uint32_t timeout_ms)
{ (void)timeout_ms; return ((FakeHal *)ctx)->poll_status; }
static Stm32AdcHalStatus read_value(void *ctx, uint32_t *value)
{ *value = ((FakeHal *)ctx)->value; return STM32_ADC_HAL_OK; }
static Stm32AdcHalStatus stop(void *ctx)
{ ((FakeHal *)ctx)->stop_count++; return STM32_ADC_HAL_OK; }

int main(void)
{
    Stm32AdcHalOps ops = { configure, start, poll, read_value, stop };
    FakeHal hal = { 0, 2047, STM32_ADC_HAL_OK, 0 };
    Stm32AdcAdapter adapter;
    AdcPort port;
    uint16_t raw = 0;

    assert(adc_port_stm32_init(&adapter, &hal, &ops, 9, 5, &port) == PORT_OK);
    assert(adc_port_read(&port, ADC_CHANNEL_BATTERY, &raw) == PORT_OK);
    assert(raw == 2047);
    assert(hal.configured_channel == 9);
    assert(hal.stop_count == 1);

    hal.poll_status = STM32_ADC_HAL_TIMEOUT;
    assert(adc_port_read(&port, ADC_CHANNEL_BATTERY, &raw)
           == PORT_STATUS_TIMEOUT);
    assert(hal.stop_count == 2);
    puts("STM32 ADC Adapter Tests: PASS");
    return 0;
}
