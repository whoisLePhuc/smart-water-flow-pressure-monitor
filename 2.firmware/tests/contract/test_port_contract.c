#include "ports/adc_port.h"
#include "ports/storage_port.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0, tests_failed = 0;
#define TEST(n) printf("  TEST: %s ... ", n)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(m) do { printf("FAIL: %s\n", m); tests_failed++; } while(0)

/* Declarations from the Linux fake */
void adc_port_linux_set_value(uint16_t value);
void adc_port_linux_set_fault(PortStatus fault);

static void test_adc_success(void)
{
    TEST("adc_success");
    adc_port_linux_set_value(2048);
    adc_port_linux_set_fault(PORT_OK);
    uint16_t raw;
    PortStatus s = adc_port_read(ADC_CHANNEL_BATTERY, &raw);
    assert(s == PORT_OK);
    assert(raw == 2048);
    PASS();
}

static void test_adc_invalid_channel(void)
{
    TEST("adc_invalid_channel");
    uint16_t raw;
    PortStatus s = adc_port_read((AdcChannel)99, &raw);
    assert(s == PORT_STATUS_INVALID_PARAM);
    PASS();
}

static void test_adc_null_param(void)
{
    TEST("adc_null_param");
    PortStatus s = adc_port_read(ADC_CHANNEL_BATTERY, NULL);
    assert(s == PORT_STATUS_INVALID_PARAM);
    PASS();
}

static void test_storage_roundtrip(void)
{
    TEST("storage_roundtrip");
    uint8_t data[] = { 0xAA, 0xBB, 0xCC, 0xDD };
    PortStatus s = storage_port_write(0, data, sizeof(data));
    assert(s == PORT_OK);
    uint8_t buf[4];
    s = storage_port_read(0, buf, sizeof(buf));
    assert(s == PORT_OK);
    assert(memcmp(data, buf, sizeof(data)) == 0);
    PASS();
}

static void test_storage_null_buffer(void)
{
    TEST("storage_null_buffer");
    PortStatus s = storage_port_read(0, NULL, 4);
    assert(s == PORT_STATUS_INVALID_PARAM);
    s = storage_port_write(0, NULL, 4);
    assert(s == PORT_STATUS_INVALID_PARAM);
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
