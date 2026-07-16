#ifndef SWFPM_SPI_BUS_MANAGER_H
#define SWFPM_SPI_BUS_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#define SPI_BUS_MAX_CLIENTS 4u
#define SPI_BUS_PENDING_CAPACITY 8u

typedef enum {
    SPI_TRANSACTION_OK,
    SPI_TRANSACTION_TIMEOUT,
    SPI_TRANSACTION_FAILED,
    SPI_TRANSACTION_CANCELLED
} SpiTransactionResult;

typedef struct {
    uint32_t client_id;
    uint32_t transaction_id;
    uint32_t correlation_id;
    uint32_t client_generation;
    uint32_t bus_generation;
    SpiTransactionResult result;
} SpiTransactionCompletion;

typedef struct {
    uint32_t client_id;
    uint8_t chip_select;
    uint32_t client_generation;
    void *context;
    bool (*start)(void *context, uint8_t chip_select,
                  const uint8_t *tx, uint8_t *rx, uint16_t length,
                  uint32_t transaction_id, uint32_t correlation_id,
                  uint32_t client_generation, uint32_t bus_generation,
                  uint64_t deadline_us);
    void (*complete)(void *context,
                     const SpiTransactionCompletion *completion);
} SpiBusClient;

typedef struct {
    uint32_t client_id;
    uint32_t transaction_id;
    uint32_t correlation_id;
    uint32_t client_generation;
    uint8_t chip_select;
    const uint8_t *tx;
    uint8_t *rx;
    uint16_t length;
    uint64_t deadline_us;
    uint8_t priority;
    uint64_t admission_sequence;
} SpiPendingTransaction;

typedef struct {
    SpiBusClient clients[SPI_BUS_MAX_CLIENTS];
    uint8_t client_count;
    SpiPendingTransaction active;
    SpiPendingTransaction pending[SPI_BUS_PENDING_CAPACITY];
    uint8_t pending_count;
    bool busy;
    uint32_t bus_generation;
    uint64_t next_admission_sequence;
    uint32_t completed_count;
    uint32_t timeout_count;
    uint32_t stale_completion_count;
    uint32_t rejected_count;
} SpiBusManager;

void spi_bus_init(SpiBusManager *bus);
bool spi_bus_register_client(SpiBusManager *bus, const SpiBusClient *client);
bool spi_bus_submit(SpiBusManager *bus,
                    uint32_t client_id,
                    uint32_t transaction_id,
                    uint32_t correlation_id,
                    const uint8_t *tx,
                    uint8_t *rx,
                    uint16_t length,
                    uint64_t deadline_us,
                    uint8_t priority);
bool spi_bus_complete(SpiBusManager *bus,
                      uint32_t transaction_id,
                      uint32_t correlation_id,
                      uint32_t client_generation,
                      uint32_t bus_generation,
                      SpiTransactionResult result);
bool spi_bus_tick(SpiBusManager *bus, uint64_t now_us);
void spi_bus_recover(SpiBusManager *bus);
const SpiPendingTransaction *spi_bus_active(const SpiBusManager *bus);

#endif /* SWFPM_SPI_BUS_MANAGER_H */
