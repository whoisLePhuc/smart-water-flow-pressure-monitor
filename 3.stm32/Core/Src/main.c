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
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef FIRMWARE_BUILD_TESTS_FM24CL04B
#include "fram_test.h"
#endif

#if defined(FIRMWARE_BUILD_TESTS_MAX35103) && \
    defined(FIRMWARE_BUILD_MAX35103_AUTOCAL)
#error "MAX35103 HIL test and AutoCal cannot run in the same build"
#endif

#if defined(FIRMWARE_BUILD_TESTS_MAX35103) || \
    defined(FIRMWARE_BUILD_MAX35103_AUTOCAL)
#include "max35103.h"
#include "max35103_stm32_hal.h"
#endif

#ifdef FIRMWARE_BUILD_TESTS_MAX35103
#include "max35103_test.h"
#endif

#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
#include "max35103_autocal.h"
#endif

#ifdef FIRMWARE_BUILD_TESTS_ZSSC3241
#include "zssc3241_test.h"
#endif
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

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
#if defined(FIRMWARE_BUILD_TESTS_MAX35103) || \
    defined(FIRMWARE_BUILD_MAX35103_AUTOCAL)
static const Max35103Profile g_max35103_profile = {
    .profile_id = 1U,
    .profile_version = 1U,
    .event_mode_cmd = MAX35103_CMD_EVTMG2,
    .tof1 = 0x1210U,
    .tof2 = 0x0001U,
    .tof3 = 0x0002U,
    .tof4 = 0x0003U,
    .tof5 = 0x0004U,
    .tof6 = 0x0005U,
    .tof7 = 0x0006U,
    .event_timing_1 = 0x0100U,
    .event_timing_2 = MAX35103_EVT2_TEMP_T1_T3,
    .tof_measurement_delay = 0x0020U,
    .calibration_control = MAX35103_CAL_CTRL_INT_EN,
    .init_timeout_ms = 20U,
    .result_timeout_ms = 20U,
    .halt_timeout_ms = 20U,
    .reference_resistance_milliohm = 1000000U,
    .rtd_nominal_resistance_milliohm = 100000U,
};
static Max35103Stm32HalContext g_max35103_hal_context;
static Max35103Transport g_max35103_transport;
#endif

#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
#define MAX35103_ACOUSTIC_PATH_UM        15000U
#define MAX35103_TRANSDUCER_FREQUENCY_HZ 1000000U

static Max35103Driver g_max35103_driver;
static Max35103AutoCalibrator g_autocal_calibrator;
static bool g_autocal_active;
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

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
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
#endif /* FIRMWARE_BUILD_TESTS */

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
  MAX35103_Test_RunAll(&g_max35103_transport, &g_max35103_profile);
#endif /* FIRMWARE_BUILD_TESTS_MAX35103 */

#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
  /* Initialise transport and driver — board-specific setup */
  if (MAX35103_Stm32HalInitTransport(
      &g_max35103_hal_context, &hspi1,
      MAX_NSS_GPIO_Port, MAX_NSS_Pin,
      MAX_RST_GPIO_Port, MAX_RST_Pin,
      &g_max35103_transport) != MAX35103_OK)
  {
    Error_Handler();
  }
  if (MAX35103_Init(&g_max35103_driver, &g_max35103_transport) != MAX35103_OK)
  {
    Error_Handler();
  }

  /* Init autocal backbone (non-blocking state machine) */
  Max35103AutoCalConfig config;
  MAX35103_AutoCalDefaultConfig(&config,
      MAX35103_ACOUSTIC_PATH_UM, MAX35103_TRANSDUCER_FREQUENCY_HZ);
  config.dpl_min = 1U;
  config.dpl_max = 1U;
  config.dly_min = 0x001CU;
  config.dly_max = 0x0023U;
  config.dly_coarse_step = 1U;
  config.dly_fine_step = 1U;

  Max35103AutoCalBackend backend;
  MAX35103_AutoCalBindDriver(&g_max35103_driver, &backend);

  static Max35103AutoCalSample s_samples[128U];
  MAX35103_AutoCalInit(&g_autocal_calibrator, &backend, &config,
      &g_max35103_profile, s_samples, 128U);

  if (MAX35103_AutoCalStart(&g_autocal_calibrator) == MAX35103_AUTOCAL_RUNNING)
  {
    g_autocal_active = true;
  }
  else
  {
    Error_Handler();
  }
#endif /* FIRMWARE_BUILD_MAX35103_AUTOCAL */

#ifdef FIRMWARE_BUILD_TESTS_ZSSC3241
  ZSSC3241_Test_RunAll(&g_zssc3241_test_config);
#endif /* FIRMWARE_BUILD_TESTS_ZSSC3241 */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  //uint32_t mv;
  //uint32_t raw;
  while (1)
  {
    // raw = ADC_Read_Voltage();
    // mv = (raw * 6600) / 4095;  // mV
    // char buffer[50];
    // int len = sprintf(buffer, "ADC Raw: %lu, Voltage: %lu mV\r\n", raw, mv);
    // HAL_UART_Transmit(&huart2, (uint8_t*)buffer, len, HAL_MAX_DELAY);
    // HAL_Delay(100);

#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
    if (g_autocal_active)
    {
      const Max35103AutoCalStatus status =
          MAX35103_AutoCalStep(&g_autocal_calibrator);

      Max35103AutoCalProgress progress;
      MAX35103_AutoCalGetProgress(&g_autocal_calibrator, &progress);

      /* Periodic progress log (every 10 evaluated candidates) */
      static uint32_t s_last_logged_eval = 0U;
      if (progress.evaluated_candidate_count >=
          s_last_logged_eval + 10U)
      {
        char buf[96];
        const int len = snprintf(buf, sizeof(buf),
            "AUTOCAL|state=%s|cand=%lu/%lu|eval=%lu|msr=%lu\r\n",
            MAX35103_AutoCalStateName(progress.state),
            (unsigned long)progress.candidate_index,
            (unsigned long)progress.candidate_count,
            (unsigned long)progress.evaluated_candidate_count,
            (unsigned long)progress.attempted_measurement_count);
        if (len > 0)
        {
          (void)HAL_UART_Transmit(&huart2, (uint8_t *)buf,
              (uint16_t)len, HAL_MAX_DELAY);
        }
        s_last_logged_eval = progress.evaluated_candidate_count;
      }

      if (status == MAX35103_AUTOCAL_COMPLETE)
      {
        Max35103AutoCalReport report;
        MAX35103_AutoCalGetReport(&g_autocal_calibrator, &report);

        char buf[96];
        int len = snprintf(buf, sizeof(buf),
            "AUTOCAL|PASS|confidence=%u|valid=%u/1000"
            "|perturb=%u/%u|crc=%08lX\r\n",
            (unsigned)report.confidence,
            (unsigned)report.verification.valid_rate_per_mille,
            (unsigned)report.perturbation_passed,
            (unsigned)report.perturbation_tested,
            (unsigned long)report.evidence_crc32);
        if (len > 0)
        {
          (void)HAL_UART_Transmit(&huart2, (uint8_t *)buf,
              (uint16_t)len, HAL_MAX_DELAY);
        }

        (void)MAX35103_Configure(&g_max35103_driver,
            &report.selected_profile);
        g_autocal_active = false;
      }
      else if (status < 0)
      {
        char buf[64];
        const int len = snprintf(buf, sizeof(buf),
            "AUTOCAL|FAIL|status=%d\r\n", (int)status);
        if (len > 0)
        {
          (void)HAL_UART_Transmit(&huart2, (uint8_t *)buf,
              (uint16_t)len, HAL_MAX_DELAY);
        }
        g_autocal_active = false;
      }
    }
#endif /* FIRMWARE_BUILD_MAX35103_AUTOCAL */

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
    uint32_t adc_value = 0;
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY) == HAL_OK)
    {
        adc_value = HAL_ADC_GetValue(&hadc1);
    }
    HAL_ADC_Stop(&hadc1);
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
