#include "drivers/storage/fram_driver.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t memory[FM24CL04B_SIZE_BYTES];
    I2cPortRequest request;
    uint8_t address_trace[32];
    uint8_t trace_count;
    uint32_t cancellation_count;
    bool active;
} FakeI2cHardware;

typedef struct {
    StorageIoCompletion last;
    uint32_t count;
} CompletionSink;

static PortStatus submit(void *context, const I2cPortRequest *request)
{
    FakeI2cHardware *hardware = context;
    if (hardware->active)
        return PORT_STATUS_BUSY;
    hardware->request = *request;
    hardware->address_trace[hardware->trace_count++] =
        request->slave_address;
    hardware->active = true;
    return PORT_OK;
}

static PortStatus cancel(void *context, uint32_t transaction_id,
                         uint32_t bus_generation)
{
    FakeI2cHardware *hardware = context;
    if (!hardware->active ||
        hardware->request.transaction_id != transaction_id ||
        hardware->request.bus_generation != bus_generation)
        return PORT_STATUS_INVALID_PARAM;
    hardware->active = false;
    hardware->cancellation_count++;
    return PORT_OK;
}

static PortStatus recover(void *context, uint32_t new_bus_generation)
{
    FakeI2cHardware *hardware = context;
    (void)new_bus_generation;
    hardware->active = false;
    return PORT_OK;
}

static void on_storage_complete(
    void *context,
    const StorageIoCompletion *completion)
{
    CompletionSink *sink = context;
    sink->last = *completion;
    sink->count++;
}

static void complete_active(FakeI2cHardware *hardware,
                            I2cBusManager *bus,
                            PortStatus result)
{
    assert(hardware->active);
    I2cPortRequest request = hardware->request;
    if (result == PORT_OK) {
        assert(request.tx && request.tx_length >= 1u);
        uint16_t address = (uint16_t)(
            ((uint16_t)(request.slave_address & 0x01u) << 8u) |
            request.tx[0]);
        if (request.tx_length > 1u) {
            uint16_t length = (uint16_t)(request.tx_length - 1u);
            assert(length <= FM24CL04B_SIZE_BYTES - address);
            memcpy(hardware->memory + address, request.tx + 1u, length);
        }
        if (request.rx_length > 0u) {
            assert(request.rx);
            assert(request.rx_length <= FM24CL04B_SIZE_BYTES - address);
            memcpy(request.rx, hardware->memory + address,
                   request.rx_length);
        }
    }
    hardware->active = false;
    assert(i2c_bus_on_port_completion(bus, &request, result));
}

static void pump_success(FakeI2cHardware *hardware, I2cBusManager *bus)
{
    unsigned guard = 0u;
    while (hardware->active) {
        complete_active(hardware, bus, PORT_OK);
        assert(++guard < 32u);
    }
}

int main(void)
{
    FakeI2cHardware hardware;
    CompletionSink sink;
    memset(&hardware, 0, sizeof(hardware));
    memset(&sink, 0, sizeof(sink));

    I2cPort physical_port = {
        .context = &hardware,
        .submit = submit,
        .cancel = cancel,
        .recover = recover
    };
    I2cBusManager bus;
    i2c_bus_init(&bus, &physical_port);

    FramConfig config = {
        .client_id = 2u,
        .slave_address_base_7bit = 0x50u,
        .capacity_bytes = FM24CL04B_SIZE_BYTES,
        .max_chunk_bytes = 16u,
        .bus_priority = 4u
    };
    FramDriver driver;
    assert(fram_init(&driver, &bus, &config) ==
           STORAGE_IO_SUBMIT_ACCEPTED);
    StoragePort storage_port;
    assert(fram_make_storage_port(&driver, &storage_port));
    assert(storage_port.bind_completion(storage_port.context,
                                        on_storage_complete, &sink));

    uint8_t written[40];
    for (uint8_t i = 0u; i < sizeof(written); ++i)
        written[i] = (uint8_t)(0x80u + i);
    StorageOperationToken token = { 1u, 10u, 1u };
    assert(fram_write_async(&driver, 0x0F8u, written, sizeof(written),
                            token, 1000u) == STORAGE_IO_SUBMIT_ACCEPTED);
    assert(fram_is_busy(&driver));
    assert(fram_write_async(&driver, 0u, written, 1u, token, 1000u) ==
           STORAGE_IO_SUBMIT_BUSY);
    pump_success(&hardware, &bus);
    assert(sink.count == 1u);
    assert(sink.last.result == STORAGE_IO_RESULT_OK);
    assert(sink.last.transferred_length == sizeof(written));
    assert(hardware.address_trace[0] == 0x50u);
    assert(hardware.address_trace[1] == 0x51u);
    assert(memcmp(hardware.memory + 0x0F8u, written, sizeof(written)) == 0);

    uint8_t readback[40] = {0};
    token.operation_id++;
    token.correlation_id++;
    hardware.trace_count = 0u;
    assert(fram_read_async(&driver, 0x0F8u, readback, sizeof(readback),
                           token, 2000u) == STORAGE_IO_SUBMIT_ACCEPTED);
    pump_success(&hardware, &bus);
    assert(sink.count == 2u);
    assert(memcmp(readback, written, sizeof(written)) == 0);
    assert(hardware.address_trace[0] == 0x50u);
    assert(hardware.address_trace[1] == 0x51u);

    token.operation_id++;
    token.correlation_id++;
    assert(fram_read_async(&driver, 511u, readback, 2u, token, 3000u) ==
           STORAGE_IO_SUBMIT_OUT_OF_RANGE);
    assert(sink.count == 2u);

    token.operation_id++;
    token.correlation_id++;
    hardware.trace_count = 0u;
    assert(fram_probe_async(&driver, token, 4000u) ==
           STORAGE_IO_SUBMIT_ACCEPTED);
    pump_success(&hardware, &bus);
    assert(sink.count == 3u);
    assert(sink.last.requested_length == 2u);
    assert(hardware.address_trace[0] == 0x50u);
    assert(hardware.address_trace[1] == 0x51u);

    token.operation_id++;
    token.correlation_id++;
    assert(fram_read_async(&driver, 0u, readback, 1u, token, 5000u) ==
           STORAGE_IO_SUBMIT_ACCEPTED);
    I2cPortRequest late = hardware.request;
    assert(i2c_bus_tick(&bus, 5001u));
    assert(sink.count == 4u);
    assert(sink.last.result == STORAGE_IO_RESULT_TIMEOUT);
    assert(hardware.cancellation_count == 1u);
    assert(!i2c_bus_on_port_completion(&bus, &late, PORT_OK));
    assert(sink.count == 4u);

    puts("F-RAM Driver Tests: PASS");
    return 0;
}
