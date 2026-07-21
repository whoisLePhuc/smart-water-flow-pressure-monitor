#include "ports/adc_port.h"
#include "ports/storage_port.h"
#include "adc_port_linux.h"
#include "storage_port_linux.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0, tests_failed = 0;
#define TEST(n) printf("  TEST: %s ... ", n)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(m) do { printf("FAIL: %s\n", m); tests_failed++; } while(0)

static LinuxAdcAdapter adc_adapter;
static AdcPort adc_port;
static LinuxStorageAdapter storage_adapter;
static StoragePort storage_port;
static StorageIoCompletion storage_completion;
static uint32_t storage_completion_count;

static void storage_complete(void *context,
                             const StorageIoCompletion *completion)
{
    (void)context;
    storage_completion = *completion;
    storage_completion_count++;
}

static void test_adc_success(void)
{
    TEST("adc_success");
    adc_port_linux_init(&adc_adapter, &adc_port);
    adc_port_linux_set_value(&adc_adapter, 2048u);
    adc_port_linux_set_fault(&adc_adapter, PORT_OK);
    uint16_t raw;
    PortStatus s = adc_port_read(&adc_port, ADC_CHANNEL_BATTERY, &raw);
    assert(s == PORT_OK);
    assert(raw == 2048);
    PASS();
}

static void test_adc_invalid_channel(void)
{
    TEST("adc_invalid_channel");
    uint16_t raw;
    PortStatus s = adc_port_read(&adc_port, (AdcChannel)99, &raw);
    assert(s == PORT_STATUS_INVALID_PARAM);
    PASS();
}

static void test_adc_null_param(void)
{
    TEST("adc_null_param");
    PortStatus s = adc_port_read(&adc_port, ADC_CHANNEL_BATTERY, NULL);
    assert(s == PORT_STATUS_INVALID_PARAM);
    PASS();
}

static void test_storage_roundtrip(void)
{
    TEST("storage_roundtrip");
    assert(storage_port_linux_init(&storage_adapter, &storage_port));
    assert(storage_port.bind_completion(storage_port.context,
                                        storage_complete, NULL));
    uint8_t data[] = { 0xAA, 0xBB, 0xCC, 0xDD };
    StorageOperationToken token = { 1u, 1u, 1u };
    StorageIoSubmitResult s = storage_port.write_async(
        storage_port.context, 0u, data, sizeof(data), token, 100u);
    assert(s == STORAGE_IO_SUBMIT_ACCEPTED);
    assert(storage_completion_count == 1u);
    assert(storage_completion.result == STORAGE_IO_RESULT_OK);
    uint8_t buf[4];
    token.operation_id++;
    token.correlation_id++;
    s = storage_port.read_async(storage_port.context, 0u, buf,
                                sizeof(buf), token, 200u);
    assert(s == STORAGE_IO_SUBMIT_ACCEPTED);
    assert(memcmp(data, buf, sizeof(data)) == 0);
    PASS();
}

static void test_storage_null_buffer(void)
{
    TEST("storage_null_buffer");
    StorageOperationToken token = { 3u, 3u, 1u };
    StorageIoSubmitResult s = storage_port.read_async(
        storage_port.context, 0u, NULL, 4u, token, 300u);
    assert(s == STORAGE_IO_SUBMIT_INVALID_PARAM);
    s = storage_port.write_async(storage_port.context, 0u, NULL, 4u,
                                 token, 300u);
    assert(s == STORAGE_IO_SUBMIT_INVALID_PARAM);
    PASS();
}

int main(void)
{
    printf("Port Contract Tests\n");
    printf("────────────────────\n");
    test_adc_success();
    test_adc_invalid_channel();
    test_adc_null_param();
    test_storage_roundtrip();
    test_storage_null_buffer();
    printf("────────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return (tests_failed == 0) ? 0 : 1;
}
