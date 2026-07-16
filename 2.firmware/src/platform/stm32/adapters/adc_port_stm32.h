#ifndef SWFPM_ADC_PORT_STM32_H
#define SWFPM_ADC_PORT_STM32_H

#include <stdbool.h>
#include <stdint.h>
#include "ports/adc_port.h"

/* Thin binding over STM32 HAL. Board code supplies these operations so this
 * adapter is independent of the exact STM32 family header and ADC handle. */
typedef enum {
    STM32_ADC_HAL_OK,
    STM32_ADC_HAL_BUSY,
    STM32_ADC_HAL_TIMEOUT,
    STM32_ADC_HAL_ERROR
} Stm32AdcHalStatus;

typedef struct {
    Stm32AdcHalStatus (*configure_channel)(void *hal_adc, uint32_t channel);
    Stm32AdcHalStatus (*start)(void *hal_adc);
    Stm32AdcHalStatus (*poll)(void *hal_adc, uint32_t timeout_ms);
    Stm32AdcHalStatus (*read_value)(void *hal_adc, uint32_t *value);
    Stm32AdcHalStatus (*stop)(void *hal_adc);
} Stm32AdcHalOps;

typedef struct {
    void *hal_adc;                 /* Borrowed board HAL handle. */
    const Stm32AdcHalOps *ops;     /* Borrowed immutable operations table. */
    uint32_t channel_map[ADC_CHANNEL_COUNT];
    uint32_t timeout_ms;           /* Upper bound passed to the blocking poll. */
    bool initialized;
} Stm32AdcAdapter;

// Creates a synchronous AdcPort backed by configure/start/poll/read/stop.
// hal_adc and ops are borrowed and must outlive adapter and port_out.
PortStatus adc_port_stm32_init(Stm32AdcAdapter *adapter,
                               void *hal_adc,
                               const Stm32AdcHalOps *ops,
                               uint32_t battery_channel,
                               uint32_t timeout_ms,
                               AdcPort *port_out);

#endif
