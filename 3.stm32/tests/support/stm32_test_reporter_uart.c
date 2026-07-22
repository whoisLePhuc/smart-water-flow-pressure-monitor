#include "support/stm32_test_reporter_uart.h"

#include <string.h>

static void write_line(void *context, const char *line)
{
    Stm32TestUartReporter *reporter = context;
    if (!reporter || !reporter->uart || !line)
        return;
    const size_t length = strlen(line);
    (void)HAL_UART_Transmit(reporter->uart, (uint8_t *)line,
                            (uint16_t)length, reporter->timeout_ms);
    static const uint8_t newline[] = "\r\n";
    (void)HAL_UART_Transmit(reporter->uart, (uint8_t *)newline,
                            2u, reporter->timeout_ms);
}

bool stm32_test_uart_reporter_init(Stm32TestUartReporter *reporter,
                                   UART_HandleTypeDef *uart,
                                   uint32_t timeout_ms,
                                   Stm32TestReporter *reporter_out)
{
    if (!reporter || !uart || !reporter_out || timeout_ms == 0u)
        return false;
    reporter->uart = uart;
    reporter->timeout_ms = timeout_ms;
    reporter_out->context = reporter;
    reporter_out->write_line = write_line;
    return true;
}
