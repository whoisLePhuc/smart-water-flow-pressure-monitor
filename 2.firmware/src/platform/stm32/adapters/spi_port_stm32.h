#ifndef SWFPM_SPI_PORT_STM32_H
#define SWFPM_SPI_PORT_STM32_H

#include <stdbool.h>
#include "ports/spi_port.h"

typedef struct {
    PortStatus (*start)(void *hal_spi, const SpiPortRequest *request);
    PortStatus (*cancel)(void *hal_spi);
    PortStatus (*recover)(void *hal_spi);
} Stm32SpiHalOps;

typedef void (*Stm32SpiCompletionSink)(
    void *context, const SpiPortRequest *request, PortStatus result);

typedef struct {
    void *hal_spi;
    const Stm32SpiHalOps *ops;
    Stm32SpiCompletionSink completion_sink;
    void *completion_context;
    SpiPortRequest active_request;
    bool active;
} Stm32SpiAdapter;

PortStatus spi_port_stm32_init(Stm32SpiAdapter *adapter,
                               void *hal_spi,
                               const Stm32SpiHalOps *ops,
                               Stm32SpiCompletionSink sink,
                               void *sink_context,
                               SpiPort *port_out);
void spi_port_stm32_on_complete(Stm32SpiAdapter *adapter,
                                PortStatus result);

#endif /* SWFPM_SPI_PORT_STM32_H */
