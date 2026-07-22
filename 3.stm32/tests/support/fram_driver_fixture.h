#ifndef SWFPM_FRAM_DRIVER_FIXTURE_H
#define SWFPM_FRAM_DRIVER_FIXTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "drivers/storage/fram_driver.h"
#include "support/stm32_test_platform.h"

typedef struct {
    Stm32TestPlatform *platform;
    I2cBusManager bus;
    FramDriver driver;
    StoragePort port;
    StorageIoCompletion completion;
    uint32_t next_operation_id;
    uint32_t next_correlation_id;
    uint32_t completion_count;
    bool completion_ready;
    bool initialized;
} FramDriverFixture;

bool fram_driver_fixture_init(FramDriverFixture *fixture,
                              Stm32TestPlatform *platform,
                              uint8_t base_address_7bit);
void fram_driver_fixture_poll(FramDriverFixture *fixture, uint64_t now_us);
StorageOperationToken fram_driver_fixture_token(FramDriverFixture *fixture);
bool fram_driver_fixture_take_completion(
    FramDriverFixture *fixture, StorageIoCompletion *completion_out);

StorageIoSubmitResult fram_driver_fixture_probe(
    FramDriverFixture *fixture, uint64_t deadline_us);
StorageIoSubmitResult fram_driver_fixture_read(
    FramDriverFixture *fixture, uint16_t address, uint8_t *buffer,
    uint16_t length, uint64_t deadline_us);
StorageIoSubmitResult fram_driver_fixture_write(
    FramDriverFixture *fixture, uint16_t address, const uint8_t *buffer,
    uint16_t length, uint64_t deadline_us);

#endif /* SWFPM_FRAM_DRIVER_FIXTURE_H */
