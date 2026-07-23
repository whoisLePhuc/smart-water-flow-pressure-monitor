/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : max35103_test.c
  * @brief          : MAX35103 hardware test suite using a caller-provided
  *                   SPI transport and UART2 for test output
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "max35103_test.h"
#include "main.h"             /* huart2 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Private variables ---------------------------------------------------------*/

static Max35103Driver test_driver;
static const Max35103Transport *test_transport;
static Max35103TemperatureResult last_temperature_result;
static Max35103RawResult last_tof_result;
static Max35103WaveEvidence last_wave_evidence;
static bool temperature_result_available = false;
static bool tof_result_available = false;

static uint32_t test_count = 0U;
static uint32_t pass_count = 0U;
static uint32_t fail_count = 0U;
static uint32_t skip_count = 0U;

/* UART print helpers --------------------------------------------------------*/

static void uart_print(const char *message)
{
    if (!message) {
        return;
    }

    size_t length = strlen(message);
    if (length > UINT16_MAX) {
        length = UINT16_MAX;
    }
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)message,
                            (uint16_t)length, HAL_MAX_DELAY);
}

static void uart_println(const char *message)
{
    uart_print(message);
    uart_print("\r\n");
}

static void uart_printf(const char *format, ...)
{
    char buffer[192];
    va_list args;

    va_start(args, format);
    const int result = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (result <= 0) {
        return;
    }

    size_t length = (size_t)result;
    if (length >= sizeof(buffer)) {
        length = sizeof(buffer) - 1U;
    }
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)buffer,
                            (uint16_t)length, HAL_MAX_DELAY);
}

/*
 * Print a signed 64-bit value without relying on printf long-long support.
 *
 * STM32 projects commonly link newlib-nano, whose minimal printf may not
 * implement "%lld". In that configuration, using "%lldps" can print "ldps"
 * literally and corrupt the following variadic arguments.
 */
static void uart_print_int64(int64_t value)
{
    char buffer[22];
    size_t position = sizeof(buffer);
    uint64_t magnitude;

    buffer[--position] = '\0';
    if (value < 0) {
        /* This form is also well-defined for INT64_MIN. */
        magnitude = (uint64_t)(-(value + 1)) + 1U;
    } else {
        magnitude = (uint64_t)value;
    }

    do {
        buffer[--position] = (char)('0' + (magnitude % 10U));
        magnitude /= 10U;
    } while (magnitude != 0U);

    if (value < 0) {
        buffer[--position] = '-';
    }

    uart_print(&buffer[position]);
}

/* Test runner helpers -------------------------------------------------------*/

static void test_start(const char *name)
{
    uart_printf("  TEST %s ... ", name);
    test_count++;
}

static void test_pass(void)
{
    uart_println("PASS");
    pass_count++;
}

static void test_fail(const char *reason)
{
    uart_printf("FAIL (%s)\r\n", reason);
    fail_count++;
}

static void test_fail_status(const char *operation, Max35103Status status)
{
    uart_printf("FAIL (%s: status=%d)\r\n", operation, (int)status);
    fail_count++;
}

static void test_skip(const char *reason)
{
    uart_printf("SKIP (%s)\r\n", reason);
    skip_count++;
}

/* Test helpers --------------------------------------------------------------*/

typedef struct {
    const char *name;
    uint8_t read_opcode;
    uint16_t expected;
} RegisterExpectation;

static uint8_t selected_temperature_ports(const Max35103Profile *profile)
{
    switch (profile->event_timing_2 & MAX35103_EVT2_TEMP_PORT_MASK) {
    case MAX35103_EVT2_TEMP_T1_T3:
        return MAX35103_TEMP_PORT_T1 | MAX35103_TEMP_PORT_T3;
    case MAX35103_EVT2_TEMP_T2_T4:
        return MAX35103_TEMP_PORT_T2 | MAX35103_TEMP_PORT_T4;
    case MAX35103_EVT2_TEMP_T1_T3_T2:
        return MAX35103_TEMP_PORT_T1 | MAX35103_TEMP_PORT_T2 |
               MAX35103_TEMP_PORT_T3;
    case MAX35103_EVT2_TEMP_ALL:
        return MAX35103_TEMP_PORT_T1 | MAX35103_TEMP_PORT_T2 |
               MAX35103_TEMP_PORT_T3 | MAX35103_TEMP_PORT_T4;
    default:
        return 0U;
    }
}

static void print_temperature_result(
    const Max35103TemperatureResult *result)
{
    uart_printf("\r\n    status=0x%04X selected=0x%02X valid=0x%02X"
                " open=0x%02X short=0x%02X\r\n",
                result->status_flags,
                result->selected_port_mask,
                result->valid_port_mask,
                result->open_circuit_mask,
                result->short_circuit_mask);
    uart_printf("    T1=%lu T2=%lu T3=%lu T4=%lu (Q16 cycles)\r\n",
                (unsigned long)result->port_q16[0],
                (unsigned long)result->port_q16[1],
                (unsigned long)result->port_q16[2],
                (unsigned long)result->port_q16[3]);

    if (result->rtd1_valid) {
        uart_printf("    RTD1=%lu milliohm",
                    (unsigned long)result->rtd1_resistance_milliohm);
        if (result->rtd1_temperature_valid) {
            uart_printf(" temperature=%ld millidegC",
                        (long)result->rtd1_temperature_millicelsius);
        }
        uart_print("\r\n");
    }
    if (result->rtd2_valid) {
        uart_printf("    RTD2=%lu milliohm",
                    (unsigned long)result->rtd2_resistance_milliohm);
        if (result->rtd2_temperature_valid) {
            uart_printf(" temperature=%ld millidegC",
                        (long)result->rtd2_temperature_millicelsius);
        }
        uart_print("\r\n");
    }
}

/* Test cases ----------------------------------------------------------------*/

/** Validate the caller-supplied board profile before touching hardware. */
static bool TEST_Profile(const Max35103Profile *profile)
{
    test_start("Production profile");

    const Max35103Status status =
        MAX35103_ValidateProfile(profile);
    if (status != MAX35103_OK) {
        if (profile) {
            uart_printf(
                "\r\n    TOF1=%04X TOF2=%04X TOF3=%04X TOF4=%04X "
                "TOF5=%04X TOF6=%04X TOF7=%04X DLY=%04X CAL=%04X\r\n",
                profile->tof1,
                profile->tof2,
                profile->tof3,
                profile->tof4,
                profile->tof5,
                profile->tof6,
                profile->tof7,
                profile->tof_measurement_delay,
                profile->calibration_control);
        }
        test_fail_status("MAX35103_ValidateProfile", status);
        return false;
    }

    test_pass();
    return true;
}

/** Validate the state created by MAX35103_Init without accessing hardware. */
static void TEST_InitDefaults(void)
{
    test_start("Driver initialization defaults");

    Max35103Driver local_driver;
    const Max35103Status status = MAX35103_Init(
        &local_driver, test_transport);
    if (status != MAX35103_OK ||
        MAX35103_GetState(&local_driver) != MAX35103_STATE_IDLE ||
        MAX35103_IsBusy(&local_driver) ||
        local_driver.device_ready || local_driver.configured ||
        local_driver.event_timing_active ||
        local_driver.generation != 1U) {
        test_fail("unexpected initialized state");
        return;
    }

    test_pass();
}

/** Validate NULL and invalid-argument guards without accessing hardware. */
static void TEST_InvalidArguments(void)
{
    test_start("Invalid argument guards");

    int32_t temperature_millicelsius = 0;
    Max35103Driver local_driver;
    Max35103WaveEvidence wave_evidence;
    Max35103Transport invalid_transport = {0};
    if (MAX35103_Init(NULL, test_transport) != MAX35103_INVALID_ARG ||
        MAX35103_Init(&local_driver, &invalid_transport) !=
            MAX35103_INVALID_ARG ||
        MAX35103_GetState(&local_driver) != MAX35103_STATE_UNINIT ||
        MAX35103_GetState(NULL) != MAX35103_STATE_UNINIT ||
        MAX35103_IsBusy(NULL) ||
        MAX35103_ReadReg(NULL, MAX35103_READ_TOF1, NULL) !=
            MAX35103_INVALID_ARG ||
        MAX35103_MeasureTemperature(NULL, NULL) !=
            MAX35103_INVALID_ARG ||
        MAX35103_ReadWaveEvidence(NULL, &wave_evidence) !=
            MAX35103_INVALID_ARG ||
        MAX35103_ReadWaveEvidence(&local_driver, NULL) !=
            MAX35103_INVALID_ARG ||
        MAX35103_ConfiguredHitCount(NULL) != 0U ||
        MAX35103_PlatinumRtdToMilliCelsius(
            100000U, 0U, &temperature_millicelsius) !=
            MAX35103_INVALID_ARG) {
        test_fail("one or more guards returned the wrong status");
        return;
    }

    test_pass();
}

/** Validate empty result mailboxes without accessing hardware. */
static void TEST_EmptyMailboxes(void)
{
    test_start("Empty result mailboxes");

    Max35103Driver local_driver;
    Max35103RawResult raw_result;
    Max35103TemperatureResult temperature_result;
    (void)MAX35103_Init(&local_driver, test_transport);

    if (MAX35103_HasResult(&local_driver) ||
        MAX35103_HasTemperatureResult(&local_driver) ||
        MAX35103_GetResult(&local_driver, &raw_result) !=
            MAX35103_NO_RESULT ||
        MAX35103_GetTemperatureResult(&local_driver,
                                      &temperature_result) !=
            MAX35103_NO_RESULT) {
        test_fail("an initialized mailbox was not empty");
        return;
    }

    test_pass();
}

/** Validate the PT100 conversion independently of hardware. */
static void TEST_Pt100Conversion(void)
{
    test_start("PT100 conversion at 0 degC");

    int32_t temperature_millicelsius = 0;
    const Max35103Status status = MAX35103_PlatinumRtdToMilliCelsius(
        100000U, 100000U, &temperature_millicelsius);
    if (status != MAX35103_OK || temperature_millicelsius != 0) {
        test_fail("PT100 0 degC point mismatch");
        return;
    }

    test_pass();
}

/** Validate a positive PT1000 reference point. */
static void TEST_Pt1000PositiveConversion(void)
{
    test_start("PT1000 conversion at 25 degC");

    int32_t temperature_millicelsius = 0;
    const Max35103Status status = MAX35103_PlatinumRtdToMilliCelsius(
        1097350U, 1000000U, &temperature_millicelsius);
    if (status != MAX35103_OK ||
        temperature_millicelsius < 24990 ||
        temperature_millicelsius > 25010) {
        test_fail("PT1000 25 degC point mismatch");
        return;
    }

    test_pass();
}

/** Validate negative temperature and range rejection. */
static void TEST_Pt1000NegativeAndRange(void)
{
    test_start("PT1000 negative and range limits");

    int32_t temperature_millicelsius = 0;
    Max35103Status status = MAX35103_PlatinumRtdToMilliCelsius(
        803063U, 1000000U, &temperature_millicelsius);
    if (status != MAX35103_OK ||
        temperature_millicelsius < -50010 ||
        temperature_millicelsius > -49990) {
        test_fail("PT1000 -50 degC point mismatch");
        return;
    }

    status = MAX35103_PlatinumRtdToMilliCelsius(
        1000U, 1000000U, &temperature_millicelsius);
    if (status != MAX35103_OUT_OF_RANGE) {
        test_fail("out-of-range value was accepted");
        return;
    }

    test_pass();
}

/** Reject register opcodes from the wrong command class. */
static void TEST_RegisterOpcodeGuards(void)
{
    test_start("Register opcode guards");

    Max35103Driver local_driver;
    uint16_t value = 0U;
    (void)MAX35103_Init(&local_driver, test_transport);

    if (MAX35103_ReadReg(&local_driver, MAX35103_CMD_TEMPERATURE,
                         &value) != MAX35103_INVALID_ARG ||
        MAX35103_WriteReg(&local_driver, MAX35103_READ_TOF1,
                          0U) != MAX35103_INVALID_ARG) {
        test_fail("invalid register opcode was accepted");
        return;
    }

    test_pass();
}

/** Validate BUSY protection before any SPI transaction is issued. */
static void TEST_BusyGuards(const Max35103Profile *profile)
{
    test_start("Busy-state guards");

    if (!profile) {
        test_skip("invalid profile");
        return;
    }

    Max35103Driver local_driver;
    (void)MAX35103_Init(&local_driver, test_transport);
    local_driver.device_ready = true;
    local_driver.configured = true;
    local_driver.profile = profile;
    local_driver.state = MAX35103_STATE_EVENT_RUNNING;
    local_driver.event_timing_active = true;

    Max35103TemperatureResult temperature_result;
    if (MAX35103_WriteReg(&local_driver, MAX35103_REG_TOF1,
                          profile->tof1) != MAX35103_BUSY ||
        MAX35103_MeasureTemperature(&local_driver,
                                    &temperature_result) != MAX35103_BUSY) {
        test_fail("operation was accepted while event timing was active");
        return;
    }

    test_pass();
}

/** Validate stale SPI completion tokens and host-side cancellation. */
static void TEST_StaleSpiToken(const Max35103Profile *profile)
{
    test_start("Stale SPI token rejection");

    if (!profile) {
        test_skip("invalid profile");
        return;
    }

    Max35103Driver local_driver;
    (void)MAX35103_Init(&local_driver, test_transport);
    local_driver.device_ready = true;
    local_driver.configured = true;
    local_driver.profile = profile;
    local_driver.state = MAX35103_STATE_EVENT_RUNNING;
    local_driver.event_timing_active = true;

    MAX35103_OnInt(&local_driver, 1000U);
    Max35103SpiRequest request;
    if (!MAX35103_GetPendingSpiRequest(&local_driver, &request)) {
        test_fail("interrupt did not schedule a status read");
        return;
    }

    MAX35103_OnSpiDone(&local_driver, request.token + 1U, true);
    if (!local_driver.spi_pending ||
        local_driver.stale_spi_completion_count != 1U) {
        test_fail("stale completion changed the active request");
        return;
    }

    MAX35103_Cancel(&local_driver);
    if (local_driver.spi_pending ||
        local_driver.state != MAX35103_STATE_EVENT_RUNNING) {
        test_fail("cancel did not clear the host-side transaction");
        return;
    }

    test_pass();
}

/** Validate deferred-read timeout state and evidence mailbox. */
static void TEST_DeferredTimeout(const Max35103Profile *profile)
{
    test_start("Deferred result timeout");

    if (!profile) {
        test_skip("invalid profile");
        return;
    }

    Max35103Profile local_profile = *profile;
    local_profile.event_mode_cmd = MAX35103_CMD_EVTMG2;

    Max35103Driver local_driver;
    (void)MAX35103_Init(&local_driver, test_transport);
    local_driver.device_ready = true;
    local_driver.configured = true;
    local_driver.profile = &local_profile;
    local_driver.state = MAX35103_STATE_EVENT_RUNNING;
    local_driver.event_timing_active = true;

    MAX35103_OnInt(&local_driver, 2000U);
    MAX35103_OnTimeout(&local_driver);

    Max35103RawResult result;
    if (local_driver.state != MAX35103_STATE_TIMEOUT ||
        !MAX35103_HasResult(&local_driver) ||
        MAX35103_GetResult(&local_driver, &result) != MAX35103_OK ||
        (result.status_flags & MAX35103_INT_TIMEOUT) == 0U ||
        result.valid) {
        test_fail("timeout state or evidence is invalid");
        return;
    }

    test_pass();
}

/** Pulse RST, verify POR, issue INIT, and wait for INIT completion. */
static bool TEST_ResetAndInit(bool profile_valid)
{
    test_start("Reset and INIT");

    if (!profile_valid) {
        test_skip("invalid profile");
        return false;
    }

    if (MAX35103_Init(&test_driver, test_transport) != MAX35103_OK) {
        test_fail("driver transport initialization failed");
        return false;
    }
    const Max35103Status status = MAX35103_ResetDevice(&test_driver);
    if (status != MAX35103_OK) {
        test_fail_status("MAX35103_ResetDevice", status);
        return false;
    }
    if (MAX35103_GetState(&test_driver) != MAX35103_STATE_IDLE ||
        !test_driver.device_ready) {
        test_fail("driver did not enter ready/idle state");
        return false;
    }

    test_pass();
    return true;
}

/** Write and read-verify the complete volatile configuration image. */
static bool TEST_Configure(const Max35103Profile *profile, bool device_ready)
{
    test_start("Configure and verify");

    if (!device_ready) {
        test_skip("device is not ready");
        return false;
    }

    const Max35103Status status = MAX35103_Configure(&test_driver, profile);
    if (status != MAX35103_OK) {
        test_fail_status("MAX35103_Configure", status);
        return false;
    }
    if (!test_driver.configured || test_driver.profile != profile) {
        test_fail("profile was not retained by driver");
        return false;
    }

    test_pass();
    return true;
}

/** Use the driver's non-destructive SPI presence heuristic. */
static void TEST_Probe(bool configured)
{
    test_start("SPI probe");

    if (!configured) {
        test_skip("device is not configured");
        return;
    }
    if (!MAX35103_Probe(&test_driver)) {
        test_fail("SPI device not responding or register image invalid");
        return;
    }

    test_pass();
}

/** Independently read back every active configuration register. */
static void TEST_RegisterReadback(const Max35103Profile *profile,
                                  bool configured)
{
    test_start("Configuration register readback");

    if (!configured) {
        test_skip("device is not configured");
        return;
    }

    const RegisterExpectation registers[] = {
        { "TOF1", MAX35103_READ_TOF1, profile->tof1 },
        { "TOF2", MAX35103_READ_TOF2, profile->tof2 },
        { "TOF3", MAX35103_READ_TOF3, profile->tof3 },
        { "TOF4", MAX35103_READ_TOF4, profile->tof4 },
        { "TOF5", MAX35103_READ_TOF5, profile->tof5 },
        { "TOF6", MAX35103_READ_TOF6, profile->tof6 },
        { "TOF7", MAX35103_READ_TOF7, profile->tof7 },
        { "EVT_TIMING_1", MAX35103_READ_EVT_TIMING_1,
          profile->event_timing_1 },
        { "EVT_TIMING_2", MAX35103_READ_EVT_TIMING_2,
          profile->event_timing_2 },
        { "TOF_MEAS_DELAY", MAX35103_READ_TOF_MEAS_DELAY,
          profile->tof_measurement_delay },
        { "CAL_CTRL", MAX35103_READ_CAL_CTRL,
          profile->calibration_control },
    };

    for (size_t i = 0U; i < sizeof(registers) / sizeof(registers[0]); ++i) {
        uint16_t value = 0U;
        const Max35103Status status = MAX35103_ReadReg(
            &test_driver, registers[i].read_opcode, &value);
        if (status != MAX35103_OK) {
            uart_printf("FAIL (read %s: status=%d)\r\n",
                        registers[i].name, (int)status);
            fail_count++;
            return;
        }
        if (value != registers[i].expected) {
            uart_printf("FAIL (%s expected=0x%04X actual=0x%04X)\r\n",
                        registers[i].name, registers[i].expected, value);
            fail_count++;
            return;
        }
    }

    test_pass();
}

/** Exercise the public write-and-verify path without changing configuration. */
static void TEST_WriteVerifySameValue(const Max35103Profile *profile,
                                      bool configured)
{
    test_start("Write/verify same register value");

    if (!configured) {
        test_skip("device is not configured");
        return;
    }

    const Max35103Status status = MAX35103_WriteVerifyReg(
        &test_driver, MAX35103_REG_TOF_MEAS_DELAY,
        profile->tof_measurement_delay);
    if (status != MAX35103_OK) {
        test_fail_status("MAX35103_WriteVerifyReg", status);
        return;
    }

    test_pass();
}

/** Execute a direct temperature command and retain its result. */
static void TEST_TemperatureCommand(bool configured)
{
    test_start("Direct temperature measurement");

    temperature_result_available = false;
    memset(&last_temperature_result, 0, sizeof(last_temperature_result));

    if (!configured) {
        test_skip("device is not configured");
        return;
    }

    const Max35103Status status = MAX35103_MeasureTemperature(
        &test_driver, &last_temperature_result);
    if (status != MAX35103_OK) {
        print_temperature_result(&last_temperature_result);
        test_fail_status("MAX35103_MeasureTemperature", status);
        return;
    }

    temperature_result_available = true;
    test_pass();
    print_temperature_result(&last_temperature_result);
}

/** Validate every port selected by Event Timing 2. */
static void TEST_TemperaturePorts(const Max35103Profile *profile)
{
    test_start("Temperature port validity");

    if (!temperature_result_available) {
        test_skip("no temperature result");
        return;
    }

    const uint8_t expected_ports = selected_temperature_ports(profile);
    if (!last_temperature_result.valid ||
        last_temperature_result.selected_port_mask != expected_ports ||
        (last_temperature_result.valid_port_mask & expected_ports) !=
            expected_ports ||
        last_temperature_result.open_circuit_mask != 0U ||
        last_temperature_result.short_circuit_mask != 0U) {
        print_temperature_result(&last_temperature_result);
        test_fail("one or more selected temperature ports are invalid");
        return;
    }

    test_pass();
}

/** Validate ratiometric resistance and platinum conversion when configured. */
static void TEST_RtdConversion(const Max35103Profile *profile)
{
    test_start("RTD resistance and temperature result");

    if (!temperature_result_available) {
        test_skip("no temperature result");
        return;
    }

    const bool conversion_enabled =
        profile->reference_resistance_milliohm != 0U &&
        profile->rtd_nominal_resistance_milliohm != 0U;
    if (!conversion_enabled) {
        test_skip("RREF or RTD R0 is zero in profile");
        return;
    }

    const uint8_t expected_ports = selected_temperature_ports(profile);
    const uint8_t pair1 = MAX35103_TEMP_PORT_T1 |
                          MAX35103_TEMP_PORT_T3;
    const uint8_t pair2 = MAX35103_TEMP_PORT_T2 |
                          MAX35103_TEMP_PORT_T4;
    if (conversion_enabled &&
        (((expected_ports & pair1) == pair1 &&
          (!last_temperature_result.rtd1_valid ||
           !last_temperature_result.rtd1_temperature_valid)) ||
         ((expected_ports & pair2) == pair2 &&
          (!last_temperature_result.rtd2_valid ||
           !last_temperature_result.rtd2_temperature_valid)))) {
        print_temperature_result(&last_temperature_result);
        test_fail("RTD resistance/temperature conversion invalid");
        return;
    }

    test_pass();
}

/** Execute direct TOF_DIFF and retain the driver's mailbox result. */
static void TEST_TofCommand(bool configured)
{
    test_start("Direct TOF_DIFF self-check");

    if (!configured) {
        test_skip("device is not configured");
        return;
    }

    const Max35103Status measurement_status =
        MAX35103_SelfCheck(&test_driver);

    tof_result_available = false;
    memset(&last_tof_result, 0, sizeof(last_tof_result));
    const Max35103Status mailbox_status =
        MAX35103_GetResult(&test_driver, &last_tof_result);

    if (measurement_status != MAX35103_OK) {
        uart_printf("\r\n    status=0x%04X up=",
                    last_tof_result.status_flags);
        uart_print_int64(last_tof_result.tof_up_ps);
        uart_print("ps down=");
        uart_print_int64(last_tof_result.tof_down_ps);
        uart_print("ps diff=");
        uart_print_int64(last_tof_result.tof_diff_ps);
        uart_printf("ps cycles=%u\r\n",
                    (unsigned int)last_tof_result.valid_cycle_count);
        uart_printf(
            "    raw AVGUP=%04X:%04X AVGDN=%04X:%04X "
            "DIFF=%04X:%04X RANGE_CYCLE=%04X\r\n",
            last_tof_result.avg_up_int,
            last_tof_result.avg_up_frac,
            last_tof_result.avg_down_int,
            last_tof_result.avg_down_frac,
            last_tof_result.tof_diff_int,
            last_tof_result.tof_diff_frac,
            last_tof_result.cycle_range_word);
        if (test_driver.profile) {
            uart_printf(
                "    cfg TOF1=%04X TOF2=%04X TOF3=%04X TOF4=%04X "
                "TOF5=%04X TOF6=%04X TOF7=%04X DLY=%04X CAL=%04X\r\n",
                test_driver.profile->tof1,
                test_driver.profile->tof2,
                test_driver.profile->tof3,
                test_driver.profile->tof4,
                test_driver.profile->tof5,
                test_driver.profile->tof6,
                test_driver.profile->tof7,
                test_driver.profile->tof_measurement_delay,
                test_driver.profile->calibration_control);
        }
        test_fail_status("MAX35103_SelfCheck", measurement_status);
        return;
    }
    if (mailbox_status != MAX35103_OK || !last_tof_result.valid ||
        (last_tof_result.status_flags & MAX35103_INT_TOF_COMPLETE) == 0U) {
        test_fail("TOF result mailbox is missing or invalid");
        return;
    }

    tof_result_available = true;
    test_pass();
    uart_print("    TOF up=");
    uart_print_int64(last_tof_result.tof_up_ps);
    uart_print("ps down=");
    uart_print_int64(last_tof_result.tof_down_ps);
    uart_print("ps diff=");
    uart_print_int64(last_tof_result.tof_diff_ps);
    uart_print("\r\n");
}

/**
 * Read WVR and configured per-wave hit times from the same TOF result.
 *
 * One-hit profiles can validate register access and WVR, but the auto-tuner
 * requires at least two hits to verify the acoustic period.
 */
static void TEST_WaveEvidence(void)
{
    test_start("TOF WVR/HIT wave evidence");

    memset(&last_wave_evidence, 0, sizeof(last_wave_evidence));
    if (!tof_result_available) {
        test_skip("no TOF result");
        return;
    }

    const Max35103Status status = MAX35103_ReadWaveEvidence(
        &test_driver, &last_wave_evidence);
    if (status != MAX35103_OK || !last_wave_evidence.valid) {
        test_fail_status("MAX35103_ReadWaveEvidence", status);
        return;
    }

    test_pass();
    uart_printf(
        "    WVR_UP=0x%04X WVR_DN=0x%04X hits=%u\r\n",
        last_wave_evidence.wvr_up,
        last_wave_evidence.wvr_down,
        (unsigned int)last_wave_evidence.configured_hit_count);
    for (uint8_t hit = 0U;
         hit < last_wave_evidence.configured_hit_count;
         ++hit) {
        uart_printf("    HIT%u up=",
                    (unsigned int)(hit + 1U));
        uart_print_int64(last_wave_evidence.hit_up_ps[hit]);
        uart_print("ps down=");
        uart_print_int64(last_wave_evidence.hit_down_ps[hit]);
        uart_print("\r\n");
    }
}

/** Independently validate TOF difference coherence. */
static void TEST_TofCoherence(void)
{
    test_start("TOF result coherence");

    if (!tof_result_available) {
        test_skip("no TOF result");
        return;
    }

    const int64_t expected_q16 =
        (int64_t)last_tof_result.tof_up_q16 -
        (int64_t)last_tof_result.tof_down_q16;
    int64_t error_q16 = (int64_t)last_tof_result.tof_diff_q16 -
                        expected_q16;
    if (error_q16 < 0) {
        error_q16 = -error_q16;
    }
    if (error_q16 > 1) {
        test_fail("TOF_DIFF does not match AVGUP - AVGDN");
        return;
    }

    test_pass();
}

/** Verify GetResult consumes the single-slot TOF mailbox. */
static void TEST_TofMailboxConsumed(void)
{
    test_start("TOF mailbox consumption");

    if (!tof_result_available) {
        test_skip("no TOF result");
        return;
    }

    Max35103RawResult result;
    if (MAX35103_HasResult(&test_driver) ||
        MAX35103_GetResult(&test_driver, &result) != MAX35103_NO_RESULT) {
        test_fail("TOF mailbox was not consumed exactly once");
        return;
    }

    test_pass();
}

/** Start the configured EVTMG command and stop it with HALT. */
static void TEST_EventStartHalt(const Max35103Profile *profile,
                                bool configured)
{
    test_start("Event timing START/HALT");

    if (!configured) {
        test_skip("device is not configured");
        return;
    }
    if ((profile->calibration_control & MAX35103_CAL_CTRL_INT_EN) == 0U) {
        test_skip("INT_EN is clear in profile");
        return;
    }

    Max35103Status status = MAX35103_StartEventTiming(&test_driver);
    if (status != MAX35103_OK || !test_driver.event_timing_active ||
        test_driver.state != MAX35103_STATE_EVENT_RUNNING) {
        test_fail_status("MAX35103_StartEventTiming", status);
        return;
    }

    status = MAX35103_Halt(&test_driver);
    if (status != MAX35103_OK || test_driver.event_timing_active ||
        test_driver.state != MAX35103_STATE_IDLE) {
        test_fail_status("MAX35103_Halt", status);
        return;
    }

    test_pass();
}

/** Confirm the driver remains usable after the complete suite. */
static void TEST_FinalDriverState(bool configured)
{
    test_start("Final driver state");

    if (!configured) {
        test_skip("device is not configured");
        return;
    }

    if (!test_driver.device_ready || !test_driver.configured ||
        MAX35103_IsBusy(&test_driver) ||
        MAX35103_GetState(&test_driver) != MAX35103_STATE_IDLE) {
        test_fail("driver did not finish in ready/idle state");
        return;
    }

    test_pass();
}

/* Public API ----------------------------------------------------------------*/

int MAX35103_Test_RunAll(
    const Max35103Transport *transport,
    const Max35103Profile *profile)
{
    test_count = 0U;
    pass_count = 0U;
    fail_count = 0U;
    skip_count = 0U;
    test_transport = transport;
    temperature_result_available = false;
    tof_result_available = false;
    memset(&last_temperature_result, 0, sizeof(last_temperature_result));
    memset(&last_tof_result, 0, sizeof(last_tof_result));
    memset(&last_wave_evidence, 0, sizeof(last_wave_evidence));

    uart_println("");
    uart_println("========================================");
    uart_println("  MAX35103 Hardware Test Suite");
    uart_println("========================================");
    uart_println("");

    const bool profile_valid = TEST_Profile(profile);
    TEST_InitDefaults();
    TEST_InvalidArguments();
    TEST_EmptyMailboxes();
    TEST_Pt100Conversion();
    TEST_Pt1000PositiveConversion();
    TEST_Pt1000NegativeAndRange();
    TEST_RegisterOpcodeGuards();
    TEST_BusyGuards(profile);
    TEST_StaleSpiToken(profile);
    TEST_DeferredTimeout(profile);

    const bool device_ready = TEST_ResetAndInit(profile_valid);
    const bool configured = TEST_Configure(
        profile, profile_valid && device_ready);

    TEST_Probe(configured);
    TEST_RegisterReadback(profile, configured);
    TEST_WriteVerifySameValue(profile, configured);
    TEST_TemperatureCommand(configured);
    TEST_TemperaturePorts(profile);
    TEST_RtdConversion(profile);
    TEST_TofCommand(configured);
    TEST_WaveEvidence();
    TEST_TofCoherence();
    TEST_TofMailboxConsumed();
    TEST_EventStartHalt(profile, configured);
    TEST_FinalDriverState(configured);

    uart_println("");
    uart_println("----------------------------------------");
    uart_printf("  Total: %lu  Pass: %lu  Fail: %lu  Skip: %lu\r\n",
                (unsigned long)test_count,
                (unsigned long)pass_count,
                (unsigned long)fail_count,
                (unsigned long)skip_count);
    uart_println("----------------------------------------");
    uart_println("");

    return fail_count > 0U ? 1 : 0;
}
