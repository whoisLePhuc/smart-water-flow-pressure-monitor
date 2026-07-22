#ifndef SWFPM_STM32_TEST_REPORTER_UART_H
#define SWFPM_STM32_TEST_REPORTER_UART_H

#include "stm32l4xx_hal.h"
#include "support/stm32_test_runner.h"

typedef struct {
    UART_HandleTypeDef *uart;
    uint32_t timeout_ms;
} Stm32TestUartReporter;

bool stm32_test_uart_reporter_init(Stm32TestUartReporter *reporter,
                                   UART_HandleTypeDef *uart,
                                   uint32_t timeout_ms,
                                   Stm32TestReporter *reporter_out);

#endif /* SWFPM_STM32_TEST_REPORTER_UART_H */
