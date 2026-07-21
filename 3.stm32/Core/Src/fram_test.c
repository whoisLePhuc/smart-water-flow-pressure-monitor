/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : fram_test.c
  * @brief          : FRAM hardware test suite — exercises FM24CL04B via I2C1
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "fram_test.h"
#include "fram_driver.h"
#include "main.h"            /* huart2, Error_Handler */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* Private variables ---------------------------------------------------------*/

static uint32_t test_count    = 0u;
static uint32_t pass_count    = 0u;
static uint32_t fail_count    = 0u;

/* UART print helper ---------------------------------------------------------*/

static void uart_print(const char *msg)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
}

static void uart_println(const char *msg)
{
    uart_print(msg);
    uart_print("\r\n");
}

static void uart_printf(const char *fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0)
        HAL_UART_Transmit(&huart2, (uint8_t *)buf,
                          (uint16_t)(len < (int)sizeof(buf) ? len : sizeof(buf) - 1),
                          HAL_MAX_DELAY);
}

/* Test runner macros --------------------------------------------------------*/

#define TEST_START(name)          do { \
    uart_printf("  TEST %s ... ", name); \
    test_count++; \
} while (0)

#define TEST_PASS()               do { \
    uart_println("PASS"); \
    pass_count++; \
} while (0)

#define TEST_FAIL(reason)         do { \
    uart_printf("FAIL (%s)\r\n", reason); \
    fail_count++; \
} while (0)

#define TEST_FAIL_ARG(reason, fmt, ...) do { \
    uart_printf("FAIL (" reason ": " fmt ")\r\n", ##__VA_ARGS__); \
    fail_count++; \
} while (0)

/* Test cases ----------------------------------------------------------------*/

/**
  * @brief  Probe both FRAM slave addresses.
  */
static void TEST_Probe(void)
{
    TEST_START("Probe");

    bool ok = FRAM_Probe();
    if (ok) {
        TEST_PASS();
    } else {
        TEST_FAIL("I2C device not responding");
    }
}

/**
  * @brief  Write a known byte at several key addresses, read it back.
  */
static void TEST_WriteReadByte(void)
{
    TEST_START("Write/Read single byte");

    /* Test addresses: start of each page, middle, end, boundary */
    uint16_t addrs[] = { 0u, 1u, 127u, 255u, 256u, 257u, 383u, 511u };
    uint8_t  pattern = 0xA5u;

    for (unsigned i = 0u; i < sizeof(addrs) / sizeof(addrs[0]); i++) {
        uint16_t addr = addrs[i];
        uint8_t  written = (uint8_t)(pattern + (uint8_t)addr);

        if (FRAM_Write(addr, &written, 1u) != FRAM_OK) {
            TEST_FAIL_ARG("write fail @ %u", "%u", addr);
            return;
        }

        uint8_t readback = 0u;
        if (FRAM_Read(addr, &readback, 1u) != FRAM_OK) {
            TEST_FAIL_ARG("read fail @ %u", "%u", addr);
            return;
        }

        if (readback != written) {
            TEST_FAIL_ARG("mismatch @ %u: wrote 0x%02X read 0x%02X",
                          "%u, 0x%02X, 0x%02X",
                          addr, written, readback);
            return;
        }
    }
    TEST_PASS();
}

/**
  * @brief  Write a 32-byte pattern and verify by reading back.
  */
static void TEST_Pattern(void)
{
    TEST_START("Write/Read 32-byte pattern");

    uint8_t pattern[32];
    uint8_t readback[32];

    for (unsigned i = 0u; i < sizeof(pattern); i++)
        pattern[i] = (uint8_t)(i * 7u + 0x11u);   /* pseudo-random */

    if (FRAM_Write(32u, pattern, sizeof(pattern)) != FRAM_OK) {
        TEST_FAIL("write failed");
        return;
    }

    if (FRAM_Read(32u, readback, sizeof(readback)) != FRAM_OK) {
        TEST_FAIL("read failed");
        return;
    }

    if (memcmp(pattern, readback, sizeof(pattern)) != 0) {
        TEST_FAIL("data mismatch");
        return;
    }
    TEST_PASS();
}

/**
  * @brief  Write across the page-256 boundary and verify.
  */
static void TEST_CrossPage(void)
{
    TEST_START("Cross-page boundary (250..265)");

    uint8_t data[16];
    uint8_t readback[16];

    for (unsigned i = 0u; i < sizeof(data); i++)
        data[i] = (uint8_t)(0x80u + i);

    FramStatus s = FRAM_Write(250u, data, sizeof(data));
    if (s != FRAM_OK) {
        TEST_FAIL_ARG("write failed: %d", "%d", (int)s);
        return;
    }

    s = FRAM_Read(250u, readback, sizeof(readback));
    if (s != FRAM_OK) {
        TEST_FAIL_ARG("read failed: %d", "%d", (int)s);
        return;
    }

    if (memcmp(data, readback, sizeof(data)) != 0) {
        TEST_FAIL("data mismatch across page boundary");
        return;
    }
    TEST_PASS();
}

/**
  * @brief  Fill entire FRAM with incrementing pattern, read back and verify.
  */
static void TEST_Stress(void)
{
    TEST_START("Full 512-byte stress");

    static uint8_t written[FRAM_SIZE_BYTES];
    static uint8_t readback[FRAM_SIZE_BYTES];

    /* Prepare incrementing pattern */
    for (unsigned i = 0u; i < FRAM_SIZE_BYTES; i++)
        written[i] = (uint8_t)i;

    /* Write in 64-byte chunks (to exercise repeated I2C) */
    for (uint16_t addr = 0u; addr < FRAM_SIZE_BYTES; addr += 64u) {
        uint16_t chunk = 64u;
        if (addr + chunk > FRAM_SIZE_BYTES)
            chunk = FRAM_SIZE_BYTES - addr;

        if (FRAM_Write(addr, written + addr, chunk) != FRAM_OK) {
            TEST_FAIL_ARG("write fail @ %u", "%u", addr);
            return;
        }
    }

    /* Read back and compare (also in chunks) */
    for (uint16_t addr = 0u; addr < FRAM_SIZE_BYTES; addr += 64u) {
        uint16_t chunk = 64u;
        if (addr + chunk > FRAM_SIZE_BYTES)
            chunk = FRAM_SIZE_BYTES - addr;

        if (FRAM_Read(addr, readback + addr, chunk) != FRAM_OK) {
            TEST_FAIL_ARG("read fail @ %u", "%u", addr);
            return;
        }
    }

    if (memcmp(written, readback, FRAM_SIZE_BYTES) != 0) {
        /* Find first mismatch for diagnostics */
        for (unsigned i = 0u; i < FRAM_SIZE_BYTES; i++) {
            if (written[i] != readback[i]) {
                TEST_FAIL_ARG("mismatch @ byte %u: wrote 0x%02X read 0x%02X",
                              "%u, 0x%02X, 0x%02X",
                              i, written[i], readback[i]);
                return;
            }
        }
    }
    TEST_PASS();
}

/**
  * @brief  Test hardware integrity: write unique pattern per byte,
  *         read all, verify — ensures no address line stuck or
  *         data line contention.
  */
static void TEST_Integrity(void)
{
    TEST_START("Hardware integrity (unique byte per address)");

    static uint8_t buf[FRAM_SIZE_BYTES];

    /* Write: each byte = low byte of its address XOR 0xC3 */
    for (uint16_t i = 0u; i < FRAM_SIZE_BYTES; i++)
        buf[i] = (uint8_t)((i ^ 0xC3u) & 0xFFu);

    FramStatus s = FRAM_Write(0u, buf, FRAM_SIZE_BYTES);
    if (s != FRAM_OK) {
        TEST_FAIL_ARG("write failed: %d", "%d", (int)s);
        return;
    }

    memset(buf, 0, sizeof(buf));
    s = FRAM_Read(0u, buf, FRAM_SIZE_BYTES);
    if (s != FRAM_OK) {
        TEST_FAIL_ARG("read failed: %d", "%d", (int)s);
        return;
    }

    for (uint16_t i = 0u; i < FRAM_SIZE_BYTES; i++) {
        uint8_t expected = (uint8_t)((i ^ 0xC3u) & 0xFFu);
        if (buf[i] != expected) {
            TEST_FAIL_ARG("byte %u: expected 0x%02X got 0x%02X",
                          "%u, 0x%02X, 0x%02X",
                          i, expected, buf[i]);
            return;
        }
    }
    TEST_PASS();
}

/* Public API ----------------------------------------------------------------*/

int FRAM_Test_RunAll(void)
{
    test_count = 0u;
    pass_count = 0u;
    fail_count = 0u;

    uart_println("");
    uart_println("========================================");
    uart_println("  FM24CL04B FRAM Hardware Test Suite");
    uart_println("========================================");
    uart_println("");

    /* Init driver */
    FRAM_Init();

    /* Run tests */
    TEST_Probe();
    TEST_WriteReadByte();
    TEST_Pattern();
    TEST_CrossPage();
    TEST_Stress();
    TEST_Integrity();

    /* Summary */
    uart_println("");
    uart_println("----------------------------------------");
    uart_printf("  Total: %lu  Pass: %lu  Fail: %lu\r\n",
                (unsigned long)test_count,
                (unsigned long)pass_count,
                (unsigned long)fail_count);
    uart_println("----------------------------------------");
    uart_println("");

    return (fail_count > 0u) ? 1 : 0;
}
