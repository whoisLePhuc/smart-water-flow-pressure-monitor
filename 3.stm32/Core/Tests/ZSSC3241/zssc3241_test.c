/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : zssc3241_test.c
  * @brief          : ZSSC3241 hardware test suite via I2C and UART2
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "zssc3241_test.h"
#include "main.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart2;

/* Private variables ---------------------------------------------------------*/

static Zssc3241 test_driver;
static Zssc3241Stm32HalContext test_hal_context;
static Zssc3241Transport test_transport;
static Zssc3241Config test_driver_config;
static Zssc3241Measurement last_corrected_result;
static bool corrected_result_available;
static uint16_t interface_nvm;
static bool interface_nvm_available;
static bool test_driver_initialized;
static volatile bool test_suite_running;

static uint32_t test_count;
static uint32_t pass_count;
static uint32_t fail_count;
static uint32_t skip_count;

/* UART print helpers --------------------------------------------------------*/

static void uart_print(const char *message)
{
    if (message == NULL) {
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
    char buffer[224];
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

static void test_fail_status(const char *operation, Zssc3241Status status)
{
    uart_printf("FAIL (%s: %s, status=%d)\r\n",
                operation, ZSSC3241_StatusString(status), (int)status);
    fail_count++;
}

static void test_skip(const char *reason)
{
    uart_printf("SKIP (%s)\r\n", reason);
    skip_count++;
}

/* Result print helpers ------------------------------------------------------*/

static void print_device_status(const Zssc3241DeviceStatus *status)
{
    uart_printf("\r\n    status=0x%02X mode=%u powered=%u busy=%u"
                " mem=%u conn=%u saturation=%u\r\n",
                status->raw,
                (unsigned)status->mode,
                status->powered ? 1U : 0U,
                status->busy ? 1U : 0U,
                status->memory_error ? 1U : 0U,
                status->connection_fault ? 1U : 0U,
                status->math_saturation ? 1U : 0U);
}

static void print_measurement(const Zssc3241Measurement *result)
{
    print_device_status(&result->device_status);
    uart_printf("    type=%u valid=%u corrected=%u generation=%lu\r\n",
                (unsigned)result->type,
                result->valid ? 1U : 0U,
                result->corrected ? 1U : 0U,
                (unsigned long)result->generation);

    if (result->sensor_valid) {
        uart_printf("    sensor=0x%06lX unsigned=%lu signed=%ld\r\n",
                    (unsigned long)result->sensor_raw24,
                    (unsigned long)result->sensor_raw24,
                    (long)result->sensor_signed24);
    }
    if (result->temperature_valid) {
        uart_printf("    temperature=0x%06lX unsigned=%lu signed=%ld\r\n",
                    (unsigned long)result->temperature_raw24,
                    (unsigned long)result->temperature_raw24,
                    (long)result->temperature_signed24);
    }
}

static void print_diagnostics(const Zssc3241Diagnostics *diagnostics)
{
    print_device_status(&diagnostics->device_status);
    uart_printf("    diagnosticreg=0x%04X valid=%u\r\n",
                diagnostics->raw, diagnostics->valid ? 1U : 0U);
}

/* Local helpers -------------------------------------------------------------*/

static bool config_valid(const Zssc3241TestConfig *config)
{
    if (config == NULL || config->hi2c == NULL ||
        config->address_7bit > 0x7FU) {
        return false;
    }
    if (config->address_7bit >= 0x04U &&
        config->address_7bit <= 0x07U) {
        return false;
    }
    if (config->reset_available &&
        (config->reset_port == NULL || config->reset_pin == 0U)) {
        return false;
    }
    if (config->pressure_mapping_enabled &&
        (config->pressure_code_max <= config->pressure_code_min ||
         config->pressure_code_max > UINT32_C(0x00FFFFFF))) {
        return false;
    }
    return true;
}

static Zssc3241Status initialize_transport(
    const Zssc3241TestConfig *config)
{
    return ZSSC3241_Stm32HalInitTransport(
        &test_hal_context,
        config->hi2c,
        config->reset_available ? config->reset_port : NULL,
        config->reset_available ? config->reset_pin : 0U,
        &test_transport);
}

static Zssc3241Status initialize_driver(
    Zssc3241 *driver, const Zssc3241TestConfig *config)
{
    test_driver_config = config->driver_config != NULL
        ? *config->driver_config : ZSSC3241_DefaultConfig();

    /* The hardware suite is intentionally non-destructive. */
    test_driver_config.allow_nvm_write = false;
    test_driver_config.use_eoc_interrupt = config->eoc_available;

    Zssc3241Status status = initialize_transport(config);
    if (status != ZSSC3241_OK) {
        return status;
    }

    return ZSSC3241_Init(
        driver,
        &test_transport,
        config->address_7bit,
        &test_driver_config);
}

static Zssc3241Status probe_with_timeout(Zssc3241 *driver)
{
    const uint32_t start = HAL_GetTick();
    for (;;) {
        Zssc3241Status status = ZSSC3241_Probe(driver);
        if (status != ZSSC3241_BUSY) {
            return status;
        }
        if ((uint32_t)(HAL_GetTick() - start) >=
            test_driver_config.measurement_timeout_ms) {
            return ZSSC3241_TIMEOUT;
        }
        HAL_Delay(test_driver_config.poll_interval_ms);
    }
}

static bool wait_for_async_result(
    Zssc3241 *driver, uint32_t timeout_ms, Zssc3241Status *final_status)
{
    const uint32_t start = HAL_GetTick();
    *final_status = ZSSC3241_BUSY;

    while (ZSSC3241_IsBusy(driver)) {
        *final_status = ZSSC3241_Process(driver);
        if (*final_status != ZSSC3241_BUSY) {
            break;
        }
        if ((uint32_t)(HAL_GetTick() - start) >= timeout_ms) {
            *final_status = ZSSC3241_TIMEOUT;
            return false;
        }
        HAL_Delay(test_driver_config.poll_interval_ms);
    }
    return *final_status == ZSSC3241_OK;
}

/* Software-only test cases --------------------------------------------------*/

static bool TEST_Configuration(const Zssc3241TestConfig *config)
{
    test_start("Board test configuration");

    if (!config_valid(config)) {
        test_fail("invalid I2C, address, reset, or mapping configuration");
        return false;
    }
    test_pass();
    return true;
}

static void TEST_InitDefaults(const Zssc3241TestConfig *config,
                              bool valid_config)
{
    test_start("Driver initialization defaults");
    if (!valid_config) {
        test_skip("invalid test configuration");
        return;
    }

    Zssc3241 local_driver;
    Zssc3241Status status = initialize_driver(&local_driver, config);
    if (status != ZSSC3241_OK ||
        ZSSC3241_GetState(&local_driver) != ZSSC3241_STATE_IDLE ||
        ZSSC3241_IsBusy(&local_driver) ||
        ZSSC3241_HasResult(&local_driver) ||
        local_driver.transport.context != &test_hal_context ||
        local_driver.transport.write == NULL ||
        local_driver.transport.read == NULL ||
        local_driver.transport.get_tick_ms == NULL ||
        local_driver.transport.delay_ms == NULL ||
        local_driver.address_7bit != config->address_7bit ||
        local_driver.mode != ZSSC3241_MODE_UNKNOWN ||
        local_driver.nvm_write_unlocked) {
        test_fail("unexpected initialized state");
        return;
    }
    test_pass();
}

static void TEST_InvalidArguments(const Zssc3241TestConfig *config,
                                  bool valid_config)
{
    test_start("Invalid argument guards");
    if (!valid_config) {
        test_skip("invalid test configuration");
        return;
    }

    Zssc3241 local_driver;
    Zssc3241DeviceStatus device_status;
    Zssc3241Transport invalid_transport;
    int32_t physical_value;

    if (initialize_transport(config) != ZSSC3241_OK) {
        test_fail("STM32 HAL transport initialization failed");
        return;
    }
    invalid_transport = test_transport;
    invalid_transport.read = NULL;

    if (ZSSC3241_Init(NULL, &test_transport, config->address_7bit,
                      NULL) != ZSSC3241_INVALID_ARG ||
        ZSSC3241_Init(&local_driver, NULL, config->address_7bit,
                      NULL) != ZSSC3241_INVALID_ARG ||
        ZSSC3241_Init(&local_driver, &invalid_transport,
                      config->address_7bit,
                      NULL) != ZSSC3241_INVALID_ARG ||
        ZSSC3241_ReadStatus(NULL, &device_status) !=
            ZSSC3241_INVALID_ARG ||
        ZSSC3241_MapCorrected(0U, 1U, 1U, 0, 1,
                              &physical_value) != ZSSC3241_INVALID_ARG ||
        ZSSC3241_GetState(NULL) != ZSSC3241_STATE_UNINITIALIZED) {
        test_fail("one or more guards returned the wrong status");
        return;
    }
    test_pass();
}

static void TEST_EmptyMailbox(const Zssc3241TestConfig *config,
                              bool valid_config)
{
    test_start("Empty result mailbox");
    if (!valid_config) {
        test_skip("invalid test configuration");
        return;
    }

    Zssc3241 local_driver;
    Zssc3241Measurement result;
    if (initialize_driver(&local_driver, config) != ZSSC3241_OK ||
        ZSSC3241_HasResult(&local_driver) ||
        ZSSC3241_GetLatestResult(&local_driver, &result) !=
            ZSSC3241_NO_RESULT) {
        test_fail("initialized mailbox was not empty");
        return;
    }
    test_pass();
}

static void TEST_DataConversion(void)
{
    test_start("24-bit decode and sign extension");

    const uint8_t sample[3] = {0x12U, 0x34U, 0x56U};
    if (ZSSC3241_DecodeUnsigned24(sample) != UINT32_C(0x123456) ||
        ZSSC3241_DecodeSigned24(UINT32_C(0x7FFFFF)) != 8388607 ||
        ZSSC3241_DecodeSigned24(UINT32_C(0x800000)) != -8388608 ||
        ZSSC3241_DecodeSigned24(UINT32_C(0xFFFFFF)) != -1) {
        test_fail("24-bit conversion mismatch");
        return;
    }
    test_pass();
}

static void TEST_PhysicalMappingMath(void)
{
    test_start("Corrected-code physical mapping");

    int32_t value = 0;
    Zssc3241Status status = ZSSC3241_MapCorrected(
        500U, 0U, 1000U, -10000, 10000, &value);
    if (status != ZSSC3241_OK || value != 0) {
        test_fail("midpoint mapping mismatch");
        return;
    }
    test_pass();
}

static void TEST_NvmGuards(const Zssc3241TestConfig *config,
                           bool valid_config)
{
    test_start("NVM range and write guards");
    if (!valid_config) {
        test_skip("invalid test configuration");
        return;
    }

    Zssc3241 local_driver;
    uint16_t value;
    if (initialize_driver(&local_driver, config) != ZSSC3241_OK) {
        test_fail("local driver init failed");
        return;
    }
    local_driver.mode = ZSSC3241_MODE_COMMAND;

    if (ZSSC3241_ReadNvm(&local_driver, 0x40U, &value) !=
            ZSSC3241_INVALID_ARG ||
        ZSSC3241_WriteNvm(&local_driver, 0x00U, 0U) !=
            ZSSC3241_NVM_WRITE_DISABLED ||
        ZSSC3241_UnlockNvmWrites(
            &local_driver, ZSSC3241_NVM_UNLOCK_KEY) !=
            ZSSC3241_NVM_WRITE_DISABLED) {
        test_fail("unsafe or out-of-range NVM request was accepted");
        return;
    }
    test_pass();
}

static void TEST_BusyGuards(const Zssc3241TestConfig *config,
                            bool valid_config)
{
    test_start("Busy-state guards");
    if (!valid_config) {
        test_skip("invalid test configuration");
        return;
    }

    Zssc3241 local_driver;
    uint16_t value;
    if (initialize_driver(&local_driver, config) != ZSSC3241_OK) {
        test_fail("local driver init failed");
        return;
    }
    local_driver.mode = ZSSC3241_MODE_COMMAND;
    local_driver.state = ZSSC3241_STATE_WAIT_READY;

    if (ZSSC3241_StartMeasurement(
            &local_driver, ZSSC3241_MEASUREMENT_CORRECTED) !=
            ZSSC3241_BUSY ||
        ZSSC3241_ReadNvm(&local_driver, 0x02U, &value) !=
            ZSSC3241_BUSY) {
        test_fail("operation was accepted while driver was busy");
        return;
    }
    test_pass();
}

static void TEST_DeferredTimeout(const Zssc3241TestConfig *config,
                                 bool valid_config)
{
    test_start("Deferred measurement timeout");
    if (!valid_config) {
        test_skip("invalid test configuration");
        return;
    }

    Zssc3241 local_driver;
    if (initialize_driver(&local_driver, config) != ZSSC3241_OK) {
        test_fail("local driver init failed");
        return;
    }

    local_driver.mode = ZSSC3241_MODE_COMMAND;
    local_driver.state = ZSSC3241_STATE_WAIT_READY;
    local_driver.operation_start_ms = HAL_GetTick() - 2U;
    local_driver.operation_timeout_ms = 1U;

    if (ZSSC3241_Process(&local_driver) != ZSSC3241_TIMEOUT ||
        local_driver.state != ZSSC3241_STATE_TIMEOUT ||
        local_driver.timeout_count != 1U) {
        test_fail("timeout state or counter mismatch");
        return;
    }
    test_pass();
}

/* Hardware test cases -------------------------------------------------------*/

static bool TEST_HardwareInit(const Zssc3241TestConfig *config,
                              bool valid_config)
{
    test_start("STM32 HAL driver instance");
    test_driver_initialized = false;

    if (!valid_config) {
        test_skip("invalid test configuration");
        return false;
    }

    Zssc3241Status status = initialize_driver(&test_driver, config);
    if (status != ZSSC3241_OK) {
        test_fail_status("ZSSC3241_Init", status);
        return false;
    }
    test_driver_initialized = true;
    test_pass();
    return true;
}

static bool TEST_ResetAndProbe(const Zssc3241TestConfig *config,
                               bool driver_initialized)
{
    test_start("Reset and I2C probe");
    if (!driver_initialized) {
        test_skip("driver is not initialized");
        return false;
    }

    Zssc3241Status status = config->reset_available
        ? ZSSC3241_Reset(&test_driver)
        : probe_with_timeout(&test_driver);
    if (status != ZSSC3241_OK) {
        test_fail_status(config->reset_available
                             ? "ZSSC3241_Reset" : "ZSSC3241_Probe",
                         status);
        return false;
    }
    test_pass();
    return true;
}

static bool TEST_CommandMode(bool device_ready)
{
    test_start("Enter Command Mode");
    if (!device_ready) {
        test_skip("device is not ready");
        return false;
    }

    Zssc3241Status status = ZSSC3241_EnterCommandMode(&test_driver);
    if (status != ZSSC3241_OK ||
        test_driver.mode != ZSSC3241_MODE_COMMAND ||
        ZSSC3241_GetState(&test_driver) != ZSSC3241_STATE_IDLE) {
        test_fail_status("ZSSC3241_EnterCommandMode", status);
        return false;
    }
    test_pass();
    return true;
}

static void TEST_Status(bool command_mode)
{
    test_start("General status byte");
    if (!command_mode) {
        test_skip("Command Mode was not established");
        return;
    }

    Zssc3241DeviceStatus status_byte;
    Zssc3241Status status = ZSSC3241_ReadStatus(
        &test_driver, &status_byte);
    if (status != ZSSC3241_OK) {
        test_fail_status("ZSSC3241_ReadStatus", status);
        return;
    }
    if (!status_byte.powered || status_byte.busy ||
        status_byte.mode != ZSSC3241_MODE_COMMAND ||
        status_byte.memory_error || status_byte.connection_fault ||
        status_byte.math_saturation) {
        print_device_status(&status_byte);
        test_fail("unexpected status flags");
        return;
    }
    test_pass();
    print_device_status(&status_byte);
}

static void TEST_InterfaceNvm(const Zssc3241TestConfig *config,
                              bool command_mode)
{
    test_start("NVM interface configuration");
    interface_nvm_available = false;
    interface_nvm = 0U;

    if (!command_mode) {
        test_skip("Command Mode was not established");
        return;
    }

    Zssc3241Status status = ZSSC3241_ReadNvm(
        &test_driver, ZSSC3241_NVM_INTERFACE_CONFIG, &interface_nvm);
    if (status != ZSSC3241_OK) {
        test_fail_status("ZSSC3241_ReadNvm(0x02)", status);
        return;
    }

    const uint8_t address =
        (uint8_t)(interface_nvm & ZSSC3241_NVM_SLAVE_ADDRESS_MASK);
    if (address != config->address_7bit) {
        uart_printf("FAIL (NVM address=0x%02X configured=0x%02X)\r\n",
                    address, config->address_7bit);
        fail_count++;
        return;
    }

    interface_nvm_available = true;
    test_pass();
    uart_printf("    NVM[0x02]=0x%04X address=0x%02X INT_setup=%u\r\n",
                interface_nvm, address,
                (unsigned)((interface_nvm >> 7) & 0x03U));
}

static void TEST_NvmReadback(const Zssc3241TestConfig *config,
                             bool command_mode)
{
    test_start("NVM readback");
    if (!command_mode) {
        test_skip("Command Mode was not established");
        return;
    }

    if (config->run_full_nvm_dump) {
        uint16_t nvm[ZSSC3241_NVM_WORD_COUNT];
        Zssc3241Status status = ZSSC3241_DumpNvm(
            &test_driver, nvm, ZSSC3241_NVM_WORD_COUNT);
        if (status != ZSSC3241_OK) {
            test_fail_status("ZSSC3241_DumpNvm", status);
            return;
        }
        test_pass();
        for (uint8_t address = 0U;
             address < ZSSC3241_NVM_WORD_COUNT; ++address) {
            uart_printf("    NVM[0x%02X]=0x%04X\r\n",
                        address, nvm[address]);
        }
        return;
    }

    const uint8_t addresses[] = {
        0x03U, 0x04U, 0x14U, 0x15U, 0x16U,
        0x17U, 0x1EU, 0x1FU, 0x20U, 0x21U,
    };
    uint16_t values[sizeof(addresses)];

    for (size_t i = 0U; i < sizeof(addresses); ++i) {
        Zssc3241Status status = ZSSC3241_ReadNvm(
            &test_driver, addresses[i], &values[i]);
        if (status != ZSSC3241_OK) {
            uart_printf("FAIL (read NVM[0x%02X]: %s)\r\n",
                        addresses[i], ZSSC3241_StatusString(status));
            fail_count++;
            return;
        }
    }

    test_pass();
    for (size_t i = 0U; i < sizeof(addresses); ++i) {
        uart_printf("    NVM[0x%02X]=0x%04X\r\n",
                    addresses[i], values[i]);
    }
}

static bool TEST_Diagnostics(bool command_mode)
{
    test_start("Update and read diagnostics");
    if (!command_mode) {
        test_skip("Command Mode was not established");
        return false;
    }

    Zssc3241Status status = ZSSC3241_UpdateDiagnostics(&test_driver);
    if (status != ZSSC3241_OK) {
        test_fail_status("ZSSC3241_UpdateDiagnostics", status);
        return false;
    }

    Zssc3241Diagnostics diagnostics;
    memset(&diagnostics, 0, sizeof(diagnostics));
    status = ZSSC3241_ReadDiagnostics(&test_driver, &diagnostics);
    if (status != ZSSC3241_OK || !diagnostics.valid ||
        (diagnostics.raw & ZSSC3241_DIAG_FAULT_MASK) != 0U) {
        print_diagnostics(&diagnostics);
        test_fail_status("ZSSC3241_ReadDiagnostics", status);
        return false;
    }

    test_pass();
    print_diagnostics(&diagnostics);
    return true;
}

static void TEST_RawSensor(bool command_mode)
{
    test_start("Raw sensor measurement");
    if (!command_mode) {
        test_skip("Command Mode was not established");
        return;
    }

    Zssc3241Measurement result;
    memset(&result, 0, sizeof(result));
    Zssc3241Status status = ZSSC3241_MeasureRawSensor(
        &test_driver, &result);
    if (status != ZSSC3241_OK || !result.valid ||
        !result.sensor_valid || result.temperature_valid ||
        result.sensor_raw24 > UINT32_C(0x00FFFFFF)) {
        print_measurement(&result);
        test_fail_status("ZSSC3241_MeasureRawSensor", status);
        return;
    }
    test_pass();
    print_measurement(&result);
}

static void TEST_RawTemperature(bool command_mode)
{
    test_start("Raw temperature measurement");
    if (!command_mode) {
        test_skip("Command Mode was not established");
        return;
    }

    Zssc3241Measurement result;
    memset(&result, 0, sizeof(result));
    Zssc3241Status status = ZSSC3241_MeasureRawTemperature(
        &test_driver, &result);
    if (status != ZSSC3241_OK || !result.valid ||
        !result.temperature_valid || result.sensor_valid ||
        result.temperature_raw24 > UINT32_C(0x00FFFFFF)) {
        print_measurement(&result);
        test_fail_status("ZSSC3241_MeasureRawTemperature", status);
        return;
    }
    test_pass();
    print_measurement(&result);
}

static bool TEST_CorrectedMeasurement(bool command_mode)
{
    test_start("Corrected sensor and temperature");
    corrected_result_available = false;
    memset(&last_corrected_result, 0, sizeof(last_corrected_result));

    if (!command_mode) {
        test_skip("Command Mode was not established");
        return false;
    }

    Zssc3241Status status = ZSSC3241_Measure(
        &test_driver, &last_corrected_result);
    if (status != ZSSC3241_OK || !last_corrected_result.valid ||
        !last_corrected_result.corrected ||
        !last_corrected_result.sensor_valid ||
        !last_corrected_result.temperature_valid ||
        last_corrected_result.sensor_raw24 > UINT32_C(0x00FFFFFF) ||
        last_corrected_result.temperature_raw24 >
            UINT32_C(0x00FFFFFF)) {
        print_measurement(&last_corrected_result);
        test_fail_status("ZSSC3241_Measure", status);
        return false;
    }

    corrected_result_available = true;
    test_pass();
    print_measurement(&last_corrected_result);
    return true;
}

static void TEST_PressureMapping(const Zssc3241TestConfig *config)
{
    test_start("Corrected pressure mapping");
    if (!corrected_result_available) {
        test_skip("no corrected sensor result");
        return;
    }
    if (!config->pressure_mapping_enabled) {
        test_skip("pressure code endpoints are not configured");
        return;
    }

    int32_t pressure_mbar;
    Zssc3241Status status = ZSSC3241_MapCorrected(
        last_corrected_result.sensor_raw24,
        config->pressure_code_min,
        config->pressure_code_max,
        config->pressure_min_mbar,
        config->pressure_max_mbar,
        &pressure_mbar);
    if (status != ZSSC3241_OK) {
        test_fail_status("ZSSC3241_MapCorrected", status);
        return;
    }

    test_pass();
    uart_printf("    pressure=%ld mbar (%ld.%03ld bar)\r\n",
                (long)pressure_mbar,
                (long)(pressure_mbar / 1000),
                (long)(pressure_mbar >= 0
                           ? pressure_mbar % 1000
                           : -(pressure_mbar % 1000)));
}

static void TEST_Oversampling(bool command_mode)
{
    static const uint8_t sample_counts[] = {2U, 4U, 8U, 16U};

    for (size_t i = 0U;
         i < sizeof(sample_counts) / sizeof(sample_counts[0]); ++i) {
        char name[48];
        (void)snprintf(name, sizeof(name),
                       "Oversample-%u measurement", sample_counts[i]);
        test_start(name);

        if (!command_mode) {
            test_skip("Command Mode was not established");
            continue;
        }

        Zssc3241Measurement result;
        memset(&result, 0, sizeof(result));
        Zssc3241Status status = ZSSC3241_MeasureOversampled(
            &test_driver, sample_counts[i], &result);
        if (status != ZSSC3241_OK || !result.valid ||
            !result.corrected || !result.sensor_valid ||
            !result.temperature_valid) {
            print_measurement(&result);
            test_fail_status("ZSSC3241_MeasureOversampled", status);
            continue;
        }
        test_pass();
        uart_printf("    N=%u sensor=0x%06lX temperature=0x%06lX\r\n",
                    sample_counts[i],
                    (unsigned long)result.sensor_raw24,
                    (unsigned long)result.temperature_raw24);
    }
}

static void TEST_BlockingMailboxConsumed(bool corrected_available)
{
    test_start("Blocking-result mailbox consumption");
    if (!corrected_available) {
        test_skip("no corrected measurement");
        return;
    }

    Zssc3241Measurement result;
    if (ZSSC3241_HasResult(&test_driver) ||
        ZSSC3241_GetLatestResult(&test_driver, &result) !=
            ZSSC3241_NO_RESULT) {
        test_fail("blocking measurement left a pending mailbox result");
        return;
    }
    test_pass();
}

static void TEST_AsyncMeasurement(const Zssc3241TestConfig *config,
                                  bool command_mode)
{
    test_start("Deferred measurement and EOC mailbox");
    if (!command_mode) {
        test_skip("Command Mode was not established");
        return;
    }

    const uint32_t eoc_before = test_driver.eoc_count;
    Zssc3241Status status = ZSSC3241_StartMeasurement(
        &test_driver, ZSSC3241_MEASUREMENT_CORRECTED);
    if (status != ZSSC3241_OK) {
        test_fail_status("ZSSC3241_StartMeasurement", status);
        return;
    }

    const uint32_t timeout =
        test_driver_config.measurement_timeout_ms * 2U;
    if (!wait_for_async_result(&test_driver, timeout, &status)) {
        ZSSC3241_Cancel(&test_driver);
        test_fail_status("ZSSC3241_Process", status);
        return;
    }

    Zssc3241Measurement result;
    memset(&result, 0, sizeof(result));
    status = ZSSC3241_GetLatestResult(&test_driver, &result);
    if (status != ZSSC3241_OK || !result.valid ||
        !result.corrected) {
        print_measurement(&result);
        test_fail_status("ZSSC3241_GetLatestResult", status);
        return;
    }

    if (config->eoc_available && interface_nvm_available &&
        ((interface_nvm >> 7) & 0x03U) == 0U &&
        test_driver.eoc_count == eoc_before) {
        test_fail("EOC pulse was not observed by EXTI");
        return;
    }

    test_pass();
    print_measurement(&result);
}

static void TEST_SelfDiagnostic(bool command_mode)
{
    test_start("Self-diagnostic raw measurement");
    if (!command_mode) {
        test_skip("Command Mode was not established");
        return;
    }

    Zssc3241Measurement result;
    memset(&result, 0, sizeof(result));
    Zssc3241Status status = ZSSC3241_RunSelfDiagnostic(
        &test_driver, 0U, &result);
    if (status != ZSSC3241_OK || !result.valid ||
        !result.sensor_valid ||
        result.type != ZSSC3241_MEASUREMENT_SELF_DIAGNOSTIC) {
        print_measurement(&result);
        test_fail_status("ZSSC3241_RunSelfDiagnostic", status);
        return;
    }
    test_pass();
    print_measurement(&result);
}

static void TEST_CyclicMode(const Zssc3241TestConfig *config,
                            bool command_mode)
{
    test_start("Cyclic Mode START/read/STOP");
    if (!command_mode) {
        test_skip("Command Mode was not established");
        return;
    }
    if (!config->run_cyclic_test) {
        test_skip("cyclic test is disabled");
        return;
    }

    Zssc3241Status status = ZSSC3241_StartCyclicMode(&test_driver);
    if (status != ZSSC3241_OK) {
        test_fail_status("ZSSC3241_StartCyclicMode", status);
        return;
    }

    const uint32_t settle = config->cyclic_settle_ms == 0U
        ? 50U : config->cyclic_settle_ms;
    HAL_Delay(settle);

    Zssc3241Measurement result;
    memset(&result, 0, sizeof(result));
    const uint32_t start = HAL_GetTick();
    do {
        status = ZSSC3241_ReadCyclicResult(&test_driver, &result);
        if (status == ZSSC3241_OK && result.valid) {
            break;
        }
        HAL_Delay(test_driver_config.poll_interval_ms);
    } while ((uint32_t)(HAL_GetTick() - start) <
             test_driver_config.measurement_timeout_ms);

    const Zssc3241Status stop_status =
        ZSSC3241_StopCyclicMode(&test_driver);
    if (status != ZSSC3241_OK || !result.valid ||
        stop_status != ZSSC3241_OK) {
        print_measurement(&result);
        test_fail_status(status != ZSSC3241_OK
                             ? "ZSSC3241_ReadCyclicResult"
                             : "ZSSC3241_StopCyclicMode",
                         status != ZSSC3241_OK ? status : stop_status);
        return;
    }

    test_pass();
    print_measurement(&result);
}

static void TEST_FinalDriverState(bool command_mode)
{
    test_start("Final driver state");
    if (!command_mode || !test_driver_initialized) {
        test_skip("driver was not fully initialized");
        return;
    }

    Zssc3241Status status = ZSSC3241_EnterCommandMode(&test_driver);
    if (status != ZSSC3241_OK ||
        test_driver.mode != ZSSC3241_MODE_COMMAND ||
        ZSSC3241_GetState(&test_driver) != ZSSC3241_STATE_IDLE ||
        ZSSC3241_IsBusy(&test_driver) ||
        test_driver.nvm_write_count != 0U ||
        test_driver.nvm_write_unlocked) {
        test_fail("driver did not finish safe in Command/Idle");
        return;
    }
    test_pass();
}

/* Public API ----------------------------------------------------------------*/

void ZSSC3241_Test_OnEoc(void)
{
    if (test_suite_running && test_driver_initialized) {
        ZSSC3241_OnEocInterrupt(&test_driver);
    }
}

int ZSSC3241_Test_RunAll(const Zssc3241TestConfig *config)
{
    test_count = 0U;
    pass_count = 0U;
    fail_count = 0U;
    skip_count = 0U;
    corrected_result_available = false;
    interface_nvm_available = false;
    test_driver_initialized = false;
    test_suite_running = true;
    memset(&test_driver, 0, sizeof(test_driver));
    memset(&last_corrected_result, 0, sizeof(last_corrected_result));

    uart_println("");
    uart_println("========================================");
    uart_println("  ZSSC3241 STM32 HAL Hardware Test Suite");
    uart_println("  Non-destructive: NVM writes disabled");
    uart_println("========================================");
    uart_println("");

    const bool valid_config = TEST_Configuration(config);
    TEST_InitDefaults(config, valid_config);
    TEST_InvalidArguments(config, valid_config);
    TEST_EmptyMailbox(config, valid_config);
    TEST_DataConversion();
    TEST_PhysicalMappingMath();
    TEST_NvmGuards(config, valid_config);
    TEST_BusyGuards(config, valid_config);
    TEST_DeferredTimeout(config, valid_config);

    const bool driver_initialized =
        TEST_HardwareInit(config, valid_config);
    const bool device_ready = TEST_ResetAndProbe(
        config, driver_initialized);
    const bool command_mode = TEST_CommandMode(device_ready);

    TEST_Status(command_mode);
    TEST_InterfaceNvm(config, command_mode);
    TEST_NvmReadback(config, command_mode);
    (void)TEST_Diagnostics(command_mode);
    TEST_RawSensor(command_mode);
    TEST_RawTemperature(command_mode);
    const bool corrected_available =
        TEST_CorrectedMeasurement(command_mode);
    TEST_PressureMapping(config);
    TEST_Oversampling(command_mode);
    TEST_BlockingMailboxConsumed(corrected_available);
    TEST_AsyncMeasurement(config, command_mode);
    TEST_SelfDiagnostic(command_mode);
    TEST_CyclicMode(config, command_mode);
    TEST_FinalDriverState(command_mode);

    test_suite_running = false;

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