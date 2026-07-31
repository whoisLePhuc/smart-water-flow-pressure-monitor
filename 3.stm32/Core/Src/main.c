/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>

#ifdef FIRMWARE_BUILD_TESTS_FM24CL04B
#include "fram_test.h"
#endif

#if defined(FIRMWARE_BUILD_TESTS_MAX35103) && defined(FIRMWARE_BUILD_MAX35103_AUTOCAL)
#error "MAX35103 HIL test and AutoCal cannot run in the same build"
#endif

#if defined(FIRMWARE_BUILD_TESTS_MAX35103) || defined(FIRMWARE_BUILD_MAX35103_AUTOCAL)
#include "max35103.h"
#include "max35103_stm32_hal.h"
#endif

#ifdef FIRMWARE_BUILD_TESTS_MAX35103
#include "max35103_test.h"
#endif

#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
#include "max35103_autocal.h"
#include "autocal_board.h"
#endif

#ifdef FIRMWARE_BUILD_TESTS_ZSSC3241
#include "zssc3241_test.h"
#endif
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
/** Application-level operating state for MAX35103 calibration and measurement. */
typedef enum
{
    APP_MAX35103_STATE_AUTOCAL = 0,
    APP_MAX35103_STATE_MEASURING,
    APP_MAX35103_STATE_RECOVERING,
    APP_MAX35103_STATE_FAULT
} AppMax35103State;
#endif
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
#define APP_MAX35103_MEASUREMENT_PERIOD_MS      1000U
#define APP_MAX35103_RETRY_DELAY_MS             100U
#define APP_MAX35103_FAULT_LOG_PERIOD_MS        5000U
#define APP_MAX35103_UART_TIMEOUT_MS            100U
#define APP_MAX35103_MAX_CONSECUTIVE_ERRORS     3U
#define APP_MAX35103_ACOUSTIC_PATH_UM           INT64_C(15000)
#define APP_MAX35103_UM_PS_TO_MPS               INT64_C(1000000)
#define APP_MAX35103_Q16_SCALE                  INT64_C(65536)
#define APP_MAX35103_PS_PER_DPL_UNIT            INT64_C(500000)
#endif
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c1;

RTC_HandleTypeDef hrtc;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
#if defined(FIRMWARE_BUILD_TESTS_MAX35103) || defined(FIRMWARE_BUILD_MAX35103_AUTOCAL)
static const Max35103Profile g_max35103_seed_profile = {
    .profile_id = 1U,
    .profile_version = 1U,
    .event_mode_cmd = MAX35103_CMD_EVTMG2,
    .tof1 = 0x1813U,
    .tof2 = 0x4201U,
    .tof3 = 0x0506U,
    .tof4 = 0x0708U,
    .tof5 = 0x090AU,
    .tof6 = 0xFC0DU,
    .tof7 = 0xFC0AU,
    .event_timing_1 = 0x0100U,
    .event_timing_2 = MAX35103_EVT2_TEMP_T1_T3,
    .tof_measurement_delay = 0x0021U,
    .calibration_control = MAX35103_CAL_CTRL_INT_EN,
    .init_timeout_ms = 20U,
    .result_timeout_ms = 20U,
    .halt_timeout_ms = 20U,
    .reference_resistance_milliohm = 1000000U,
    .rtd_nominal_resistance_milliohm = 100000U,
};
Max35103Stm32HalContext g_max35103_hal_context;
Max35103Transport g_max35103_transport;
#endif

#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
static Max35103Driver g_max35103_driver;
static Max35103AutoCalReport g_max35103_autocal_report;
static AppMax35103State g_max35103_app_state = APP_MAX35103_STATE_AUTOCAL;
static uint32_t g_max35103_next_action_ms;
static uint32_t g_max35103_last_fault_log_ms;
static uint8_t g_max35103_consecutive_errors;
#endif

#ifdef FIRMWARE_BUILD_TESTS_ZSSC3241
static const Zssc3241TestConfig g_zssc3241_test_config = {
    .hi2c = &hi2c1,
    .address_7bit = 0x28U,
    .reset_port = NULL,
    .reset_pin = 0U,
    .reset_available = false,
    .eoc_available = false,
    .run_full_nvm_dump = false,
    .run_cyclic_test = false,
    .cyclic_settle_ms = 0U,
    .pressure_mapping_enabled = false,
    .pressure_code_min = 0U,
    .pressure_code_max = 0U,
    .pressure_min_mbar = 0,
    .pressure_max_mbar = 0,
    .driver_config = NULL,
};
#endif
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_RTC_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */
uint32_t ADC_Read_Voltage(void);

#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
static bool APP_TickDue(uint32_t now_ms, uint32_t deadline_ms);
static void APP_UART_Write(const char *text);
static void APP_MAX35103_LogStatus(const char *tag, Max35103Status status);
static Max35103Status APP_MAX35103_Measure(Max35103RawResult *result,
                                           Max35103WaveEvidence *wave);
static bool APP_MAX35103_ComputeWaveZeroTof(const Max35103Profile *profile,
                                            const Max35103WaveEvidence *wave,
                                            int64_t *tof_up_ps,
                                            int64_t *tof_down_ps);
static bool APP_MAX35103_ComputeSoundSpeedQ16(int64_t tof_up_ps,
                                              int64_t tof_down_ps,
                                              int32_t *speed_q16);
static void APP_MAX35103_LogMeasurement(const Max35103RawResult *raw,
                                        const Max35103WaveEvidence *wave,
                                        int64_t zero_up_ps,
                                        int64_t zero_down_ps,
                                        int32_t speed_q16);
static bool APP_MAX35103_Recover(void);
static void APP_MAX35103_Run(void);
#endif
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
static bool APP_TickDue(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void APP_UART_Write(const char *text)
{
    if (text == NULL)
    {
        return;
    }

    const size_t length = strlen(text);
    if (length == 0U)
    {
        return;
    }

    const uint16_t tx_length =
        (uint16_t)(length > UINT16_MAX ? UINT16_MAX : length);

    (void)HAL_UART_Transmit(&huart2,
                           (uint8_t *)text,
                           tx_length,
                           APP_MAX35103_UART_TIMEOUT_MS);
}

static void APP_MAX35103_LogStatus(const char *tag, Max35103Status status)
{
    char buffer[96];
    const int length = snprintf(buffer,
                                sizeof(buffer),
                                "MEAS|%s|status=%d|driver_state=%d\r\n",
                                tag != NULL ? tag : "UNKNOWN",
                                (int)status,
                                (int)MAX35103_GetState(&g_max35103_driver));

    if (length > 0)
    {
        APP_UART_Write(buffer);
    }
}

static uint8_t APP_MAX35103_ProfileDpl(const Max35103Profile *profile)
{
    return (uint8_t)((profile->tof1 & MAX35103_TOF1_DPL_MASK) >> 4);
}

static uint8_t APP_MAX35103_ProfileHitWave(const Max35103Profile *profile,
                                           uint8_t hit_index)
{
    const uint16_t words[MAX35103_WAVE_HIT_COUNT / 2U] = {
        profile->tof3,
        profile->tof4,
        profile->tof5,
    };

    const uint16_t word = words[hit_index / 2U];
    uint8_t wave = (hit_index & 1U) == 0U
                       ? (uint8_t)((word >> 8) & MAX35103_TOF_WAVE_SELECT_MASK)
                       : (uint8_t)(word & MAX35103_TOF_WAVE_SELECT_MASK);

    const uint8_t earliest_wave = (uint8_t)(hit_index + 3U);
    if (wave < earliest_wave)
    {
        wave = earliest_wave;
    }

    return wave;
}

static bool APP_MAX35103_ComputeWaveZeroTof(const Max35103Profile *profile,
                                            const Max35103WaveEvidence *wave,
                                            int64_t *tof_up_ps,
                                            int64_t *tof_down_ps)
{
    if (profile == NULL || wave == NULL || tof_up_ps == NULL || tof_down_ps == NULL ||
        !wave->valid || wave->configured_hit_count == 0U ||
        wave->configured_hit_count > MAX35103_WAVE_HIT_COUNT)
    {
        return false;
    }

    const int64_t period_ps =
        ((int64_t)APP_MAX35103_ProfileDpl(profile) + INT64_C(1)) *
        APP_MAX35103_PS_PER_DPL_UNIT;

    int64_t up_sum_ps = 0;
    int64_t down_sum_ps = 0;

    for (uint8_t hit = 0U; hit < wave->configured_hit_count; ++hit)
    {
        const int64_t wave_delay_ps =
            (int64_t)APP_MAX35103_ProfileHitWave(profile, hit) * period_ps;
        const int64_t up_ps = wave->hit_up_ps[hit] - wave_delay_ps;
        const int64_t down_ps = wave->hit_down_ps[hit] - wave_delay_ps;

        if (up_ps <= 0 || down_ps <= 0)
        {
            return false;
        }

        up_sum_ps += up_ps;
        down_sum_ps += down_ps;
    }

    *tof_up_ps = up_sum_ps / (int64_t)wave->configured_hit_count;
    *tof_down_ps = down_sum_ps / (int64_t)wave->configured_hit_count;
    return true;
}

static Max35103Status APP_MAX35103_Measure(Max35103RawResult *result,
                                           Max35103WaveEvidence *wave)
{
    if (result == NULL || wave == NULL)
    {
        return MAX35103_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));
    memset(wave, 0, sizeof(*wave));

    const Max35103Status measurement_status =
        MAX35103_SelfCheck(&g_max35103_driver);

    Max35103Status mailbox_status = MAX35103_NO_RESULT;

    /*
     * SelfCheck can publish an invalid result before returning an error.
     * Always drain the mailbox so the next measurement is not blocked by
     * result_pending.
     */
    if (MAX35103_HasResult(&g_max35103_driver))
    {
        mailbox_status =
            MAX35103_GetResult(&g_max35103_driver, result);
    }

    if (measurement_status != MAX35103_OK)
    {
        return measurement_status;
    }

    if (mailbox_status != MAX35103_OK)
    {
        return mailbox_status;
    }

    if (!result->valid)
    {
        return MAX35103_DEVICE_ERROR;
    }

    const Max35103Status wave_status =
        MAX35103_ReadWaveEvidence(&g_max35103_driver, wave);

    if (wave_status != MAX35103_OK)
    {
        return wave_status;
    }

    return wave->valid ? MAX35103_OK : MAX35103_DEVICE_ERROR;
}

static bool APP_MAX35103_ComputeSoundSpeedQ16(int64_t tof_up_ps,
                                              int64_t tof_down_ps,
                                              int32_t *speed_q16)
{
    if (speed_q16 == NULL || tof_up_ps <= 0 || tof_down_ps <= 0)
    {
        return false;
    }

    const int64_t numerator_q16 =
        APP_MAX35103_ACOUSTIC_PATH_UM *
        APP_MAX35103_UM_PS_TO_MPS *
        APP_MAX35103_Q16_SCALE;

    const int64_t speed_up_q16 = numerator_q16 / tof_up_ps;
    const int64_t speed_down_q16 = numerator_q16 / tof_down_ps;
    const int64_t mean_speed_q16 =
        speed_up_q16 + (speed_down_q16 - speed_up_q16) / INT64_C(2);

    if (mean_speed_q16 <= 0 || mean_speed_q16 > INT32_MAX)
    {
        return false;
    }

    *speed_q16 = (int32_t)mean_speed_q16;
    return true;
}

static void APP_MAX35103_LogMeasurement(const Max35103RawResult *raw,
                                        const Max35103WaveEvidence *wave,
                                        int64_t zero_up_ps,
                                        int64_t zero_down_ps,
                                        int32_t speed_q16)
{
    if (raw == NULL || wave == NULL)
    {
        return;
    }

    int32_t speed_integer = speed_q16 >> 16;
    uint32_t speed_milli =
        (uint32_t)((((int64_t)(speed_q16 & 0xFFFF) * INT64_C(1000)) + INT64_C(32768)) >> 16);

    if (speed_milli >= 1000U)
    {
        speed_integer++;
        speed_milli -= 1000U;
    }

    const int32_t raw_up_ns = (int32_t)(raw->tof_up_ps / INT64_C(1000));
    const int32_t raw_down_ns = (int32_t)(raw->tof_down_ps / INT64_C(1000));
    const int32_t zero_up_ns = (int32_t)(zero_up_ps / INT64_C(1000));
    const int32_t zero_down_ns = (int32_t)(zero_down_ps / INT64_C(1000));
    const int32_t direction_delta_ns =
        (int32_t)((zero_up_ps - zero_down_ps) / INT64_C(1000));

    char buffer[240];
    const int length = snprintf(
        buffer,
        sizeof(buffer),
        "MEAS|RAW_UP_NS=%ld|RAW_DN_NS=%ld"
        "|ZERO_UP_NS=%ld|ZERO_DN_NS=%ld|DIR_DELTA_NS=%ld"
        "|SPEED_Q16=%ld|SPEED_MPS=%ld.%03lu"
        "|cycles=%u|range=%u|WVR_UP=%u,%u|WVR_DN=%u,%u\r\n",
        (long)raw_up_ns,
        (long)raw_down_ns,
        (long)zero_up_ns,
        (long)zero_down_ns,
        (long)direction_delta_ns,
        (long)speed_q16,
        (long)speed_integer,
        (unsigned long)speed_milli,
        (unsigned)raw->valid_cycle_count,
        (unsigned)raw->tof_range,
        (unsigned)wave->wvr_up_t1_t2_q7,
        (unsigned)wave->wvr_up_t2_ideal_q7,
        (unsigned)wave->wvr_down_t1_t2_q7,
        (unsigned)wave->wvr_down_t2_ideal_q7);

    if (length > 0)
    {
        APP_UART_Write(buffer);
    }
}

static bool APP_MAX35103_Recover(void)
{
    if (MAX35103_ResetDevice(&g_max35103_driver) != MAX35103_OK)
    {
        return false;
    }

    return MAX35103_Configure(
               &g_max35103_driver,
               &g_max35103_autocal_report.selected_profile) == MAX35103_OK;
}

static void APP_MAX35103_Run(void)
{
    const uint32_t now_ms = HAL_GetTick();

    switch (g_max35103_app_state)
    {
    case APP_MAX35103_STATE_AUTOCAL: {
        const Max35103AutoCalStatus status = AUTOCAL_Poll();

        if (status == MAX35103_AUTOCAL_COMPLETE)
        {
            if (AUTOCAL_GetReport(&g_max35103_autocal_report))
            {
                g_max35103_consecutive_errors = 0U;
                g_max35103_next_action_ms = now_ms;
                g_max35103_app_state = APP_MAX35103_STATE_MEASURING;
                APP_UART_Write("MEAS|STATE=MEASURING\r\n");
            }
            else
            {
                g_max35103_app_state = APP_MAX35103_STATE_FAULT;
                APP_UART_Write("MEAS|FAULT=AUTOCAL_REPORT_UNAVAILABLE\r\n");
            }
        }
        else if (status < MAX35103_AUTOCAL_OK)
        {
            g_max35103_app_state = APP_MAX35103_STATE_FAULT;
            APP_UART_Write("MEAS|FAULT=AUTOCAL_FAILED\r\n");
        }
        break;
    }

    case APP_MAX35103_STATE_MEASURING: {
        if (!APP_TickDue(now_ms, g_max35103_next_action_ms))
        {
            break;
        }

        g_max35103_next_action_ms =
            now_ms + APP_MAX35103_MEASUREMENT_PERIOD_MS;

        Max35103RawResult raw;
        Max35103WaveEvidence wave;
        const Max35103Status status =
            APP_MAX35103_Measure(&raw, &wave);

        if (status == MAX35103_OK)
        {
            int64_t zero_up_ps = 0;
            int64_t zero_down_ps = 0;
            int32_t speed_q16 = 0;

            const Max35103Profile *profile =
                &g_max35103_autocal_report.selected_profile;

            const bool zero_valid =
                APP_MAX35103_ComputeWaveZeroTof(profile,
                                                &wave,
                                                &zero_up_ps,
                                                &zero_down_ps);

            const bool physical_window_valid =
                zero_valid &&
                zero_up_ps >= g_max35103_autocal_report.expected_min_tof_ps &&
                zero_up_ps <= g_max35103_autocal_report.expected_max_tof_ps &&
                zero_down_ps >= g_max35103_autocal_report.expected_min_tof_ps &&
                zero_down_ps <= g_max35103_autocal_report.expected_max_tof_ps;

            if (!physical_window_valid ||
                !APP_MAX35103_ComputeSoundSpeedQ16(zero_up_ps,
                                                   zero_down_ps,
                                                   &speed_q16))
            {
                APP_MAX35103_LogStatus("INVALID_WAVE_ZERO", MAX35103_OUT_OF_RANGE);
                if (g_max35103_consecutive_errors < UINT8_MAX)
                {
                    g_max35103_consecutive_errors++;
                }
            }
            else
            {
                g_max35103_consecutive_errors = 0U;
                APP_MAX35103_LogMeasurement(&raw,
                                            &wave,
                                            zero_up_ps,
                                            zero_down_ps,
                                            speed_q16);
            }
        }
        else if (status == MAX35103_BUSY)
        {
            APP_MAX35103_LogStatus("BUSY", status);
            g_max35103_next_action_ms = now_ms + APP_MAX35103_RETRY_DELAY_MS;
        }
        else
        {
            APP_MAX35103_LogStatus("READ_FAIL", status);
            if (g_max35103_consecutive_errors < UINT8_MAX)
            {
                g_max35103_consecutive_errors++;
            }
        }

        if (g_max35103_consecutive_errors >=
            APP_MAX35103_MAX_CONSECUTIVE_ERRORS)
        {
            g_max35103_app_state = APP_MAX35103_STATE_RECOVERING;
            APP_UART_Write("MEAS|STATE=RECOVERING\r\n");
        }
        break;
    }

    case APP_MAX35103_STATE_RECOVERING:
        if (!APP_TickDue(now_ms, g_max35103_next_action_ms))
        {
            break;
        }

        if (APP_MAX35103_Recover())
        {
            g_max35103_consecutive_errors = 0U;
            g_max35103_next_action_ms =
                now_ms + APP_MAX35103_MEASUREMENT_PERIOD_MS;
            g_max35103_app_state = APP_MAX35103_STATE_MEASURING;
            APP_UART_Write("MEAS|RECOVERY=PASS\r\n");
        }
        else
        {
            g_max35103_app_state = APP_MAX35103_STATE_FAULT;
            g_max35103_last_fault_log_ms = now_ms;
            APP_UART_Write("MEAS|RECOVERY=FAIL\r\n");
        }
        break;

    case APP_MAX35103_STATE_FAULT:
    default:
        if ((uint32_t)(now_ms - g_max35103_last_fault_log_ms) >=
            APP_MAX35103_FAULT_LOG_PERIOD_MS)
        {
            g_max35103_last_fault_log_ms = now_ms;
            APP_UART_Write("MEAS|STATE=FAULT\r\n");
        }
        break;
    }
}
#endif

uint32_t ADC_Read_Voltage(void);
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_RTC_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */
/* Run FRAM hardware tests once at startup */
#ifdef FIRMWARE_BUILD_TESTS_FM24CL04B
  FRAM_Test_RunAll();
#endif /* FIRMWARE_BUILD_TESTS_FM24CL04B */

#ifdef FIRMWARE_BUILD_TESTS_MAX35103
  if (MAX35103_Stm32HalInitTransport(
      &g_max35103_hal_context,
      &hspi1,
      MAX_NSS_GPIO_Port, MAX_NSS_Pin,
      MAX_RST_GPIO_Port, MAX_RST_Pin,
      &g_max35103_transport) != MAX35103_OK)
  {
    Error_Handler();
  }
  MAX35103_Test_RunAll(&g_max35103_transport, &g_max35103_seed_profile);
#endif /* FIRMWARE_BUILD_TESTS_MAX35103 */

#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
  g_max35103_app_state = APP_MAX35103_STATE_AUTOCAL;
  g_max35103_consecutive_errors = 0U;
  g_max35103_next_action_ms = HAL_GetTick();
  AUTOCAL_Start(&g_max35103_driver, &g_max35103_seed_profile);
#endif /* FIRMWARE_BUILD_MAX35103_AUTOCAL */

#ifdef FIRMWARE_BUILD_TESTS_ZSSC3241
  ZSSC3241_Test_RunAll(&g_zssc3241_test_config);
#endif /* FIRMWARE_BUILD_TESTS_ZSSC3241 */
/* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
while (1)
  {
#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
    APP_MAX35103_Run();
#endif

    /* Other application services can be polled here without a 1-second block. */
/* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 20;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable MSI Auto calibration
  */
  HAL_RCCEx_EnableMSIPLLMode();
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_5;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x0070133F;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};
  RTC_AlarmTypeDef sAlarm = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */

  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0x0;
  sTime.Minutes = 0x0;
  sTime.Seconds = 0x0;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  sDate.WeekDay = RTC_WEEKDAY_MONDAY;
  sDate.Month = RTC_MONTH_JANUARY;
  sDate.Date = 0x1;
  sDate.Year = 0x0;

  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable the Alarm A
  */
  sAlarm.AlarmTime.Hours = 0x0;
  sAlarm.AlarmTime.Minutes = 0x0;
  sAlarm.AlarmTime.Seconds = 0x0;
  sAlarm.AlarmTime.SubSeconds = 0x0;
  sAlarm.AlarmTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sAlarm.AlarmTime.StoreOperation = RTC_STOREOPERATION_RESET;
  sAlarm.AlarmMask = RTC_ALARMMASK_NONE;
  sAlarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
  sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
  sAlarm.AlarmDateWeekDay = 0x1;
  sAlarm.Alarm = RTC_ALARM_A;
  if (HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(MAX_NSS_GPIO_Port, MAX_NSS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(MAX_RST_GPIO_Port, MAX_RST_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(MEASURE_PIN_GPIO_Port, MEASURE_PIN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : MAX_NSS_Pin */
  GPIO_InitStruct.Pin = MAX_NSS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(MAX_NSS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MAX_RST_Pin */
  GPIO_InitStruct.Pin = MAX_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(MAX_RST_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MAX_INT_Pin */
  GPIO_InitStruct.Pin = MAX_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(MAX_INT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MAX_CMP_Pin */
  GPIO_InitStruct.Pin = MAX_CMP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(MAX_CMP_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MAX_WDO_Pin */
  GPIO_InitStruct.Pin = MAX_WDO_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(MAX_WDO_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MEASURE_PIN_Pin */
  GPIO_InitStruct.Pin = MEASURE_PIN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(MEASURE_PIN_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI2_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
uint32_t ADC_Read_Voltage(void)
{
    uint32_t adc_value = 0U;

    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
        return 0U;
    }

    if (HAL_ADC_PollForConversion(&hadc1, 100U) == HAL_OK)
    {
        adc_value = HAL_ADC_GetValue(&hadc1);
    }

    (void)HAL_ADC_Stop(&hadc1);
    return adc_value;
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
