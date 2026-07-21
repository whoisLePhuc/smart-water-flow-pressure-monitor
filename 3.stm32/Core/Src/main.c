/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : STM32 async I2C/F-RAM bring-up application
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

#include "drivers/storage/fram_driver.h"
#include "infrastructure/bus/i2c_bus_manager.h"
#include "platform/stm32/adapters/i2c_port_stm32.h"
#include "platform/stm32/stm32_i2c1_hal.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum {
  APP_FRAM_STATE_IDLE = 0,
  APP_FRAM_STATE_START_PROBE,
  APP_FRAM_STATE_WAIT_PROBE,
  APP_FRAM_STATE_START_WRITE,
  APP_FRAM_STATE_WAIT_WRITE,
  APP_FRAM_STATE_START_READ,
  APP_FRAM_STATE_WAIT_READ,
  APP_FRAM_STATE_PASS,
  APP_FRAM_STATE_FAIL
} AppFramState;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/*
 * Set to 0 before StorageService owns the F-RAM. When enabled, the bring-up
 * test overwrites APP_FRAM_TEST_LENGTH bytes starting at APP_FRAM_TEST_ADDRESS.
 */
#define APP_ENABLE_FRAM_SELF_TEST     1u

#define APP_FRAM_CLIENT_ID            1u
#define APP_FRAM_BASE_ADDRESS_7BIT    0x50u
#define APP_FRAM_BUS_PRIORITY         3u
#define APP_FRAM_OWNER_GENERATION     1u
#define APP_FRAM_OPERATION_TIMEOUT_US 250000ull

/* This range deliberately crosses the 0x0FF/0x100 address-block boundary. */
#define APP_FRAM_TEST_ADDRESS         250u
#define APP_FRAM_TEST_LENGTH          16u

#define APP_OPERATION_PROBE           1u
#define APP_OPERATION_WRITE           2u
#define APP_OPERATION_READ            3u
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
static Stm32I2c1Hal g_i2c1_hal;
static Stm32I2cAdapter g_i2c1_adapter;
static I2cPort g_i2c1_port;
static I2cBusManager g_i2c_bus;

static FramDriver g_fram;
static StoragePort g_fram_port;

static AppFramState g_fram_state = APP_FRAM_STATE_IDLE;
static uint32_t g_expected_operation_id;
static uint32_t g_unmatched_i2c_completion_count;

static const uint8_t g_fram_test_pattern[APP_FRAM_TEST_LENGTH] = {
  0x53u, 0x57u, 0x46u, 0x50u, 0x4Du, 0x2Du, 0x49u, 0x32u,
  0x43u, 0x2Du, 0x41u, 0x53u, 0x59u, 0x4Eu, 0x43u, 0x21u
};
static uint8_t g_fram_readback[APP_FRAM_TEST_LENGTH];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);
/* USER CODE BEGIN PFP */
static uint64_t app_now_us(void);
static void app_uart_printf(const char *format, ...);
static bool app_runtime_init(void);
static void app_runtime_poll(uint64_t now_us);
static void app_i2c_completion_sink(void *context,
                                    const I2cPortRequest *request,
                                    PortStatus result);
static void app_fram_completion(void *context,
                                const StorageIoCompletion *completion);
static void app_fram_test_tick(uint64_t now_us);
static void app_fram_fail(const char *reason);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/**
 * @brief Extends the 32-bit HAL millisecond tick to a 64-bit microsecond time.
 *
 * This function is called only from the cooperative main loop. Its resolution
 * remains one millisecond, but the returned value does not jump backwards when
 * HAL_GetTick() wraps.
 */
static uint64_t app_now_us(void)
{
  static uint32_t previous_tick_ms;
  static uint64_t tick_epoch_ms;
  uint32_t current_tick_ms = HAL_GetTick();

  if (current_tick_ms < previous_tick_ms)
  {
    tick_epoch_ms += (1ull << 32u);
  }

  previous_tick_ms = current_tick_ms;
  return (tick_epoch_ms + (uint64_t)current_tick_ms) * 1000ull;
}

static void app_uart_printf(const char *format, ...)
{
  char buffer[192];
  va_list arguments;

  if (format == NULL)
  {
    return;
  }

  va_start(arguments, format);
  int written = vsnprintf(buffer, sizeof(buffer), format, arguments);
  va_end(arguments);

  if (written <= 0)
  {
    return;
  }

  size_t length = (size_t)written;
  if (length >= sizeof(buffer))
  {
    length = sizeof(buffer) - 1u;
  }

  (void)HAL_UART_Transmit(&huart2, (uint8_t *)buffer,
                          (uint16_t)length, 100u);
}

static StorageOperationToken app_make_token(uint32_t operation_id)
{
  StorageOperationToken token = {
    .operation_id = operation_id,
    .correlation_id = 0x1000u + operation_id,
    .owner_generation = APP_FRAM_OWNER_GENERATION
  };

  return token;
}

static void app_fram_fail(const char *reason)
{
  if (g_fram_state == APP_FRAM_STATE_FAIL)
  {
    return;
  }

  g_fram_state = APP_FRAM_STATE_FAIL;
  app_uart_printf("[FRAM] FAIL: %s; HAL error=0x%08lX\r\n",
                  reason != NULL ? reason : "unknown",
                  (unsigned long)stm32_i2c1_hal_last_error(&g_i2c1_hal));
}

static void app_i2c_completion_sink(void *context,
                                    const I2cPortRequest *request,
                                    PortStatus result)
{
  I2cBusManager *bus = (I2cBusManager *)context;

  if (!i2c_bus_on_port_completion(bus, request, result))
  {
    g_unmatched_i2c_completion_count++;
  }
}

static void app_fram_completion(void *context,
                                const StorageIoCompletion *completion)
{
  (void)context;

  if (completion == NULL ||
      completion->token.owner_generation != APP_FRAM_OWNER_GENERATION ||
      completion->token.operation_id != g_expected_operation_id)
  {
    app_fram_fail("unexpected or stale storage completion");
    return;
  }

  if (completion->result != STORAGE_IO_RESULT_OK ||
      completion->transferred_length != completion->requested_length)
  {
    app_uart_printf("[FRAM] operation %lu failed: result=%d, bytes=%u/%u\r\n",
                    (unsigned long)completion->token.operation_id,
                    (int)completion->result,
                    (unsigned int)completion->transferred_length,
                    (unsigned int)completion->requested_length);
    app_fram_fail("asynchronous operation failed");
    return;
  }

  switch (completion->token.operation_id)
  {
    case APP_OPERATION_PROBE:
      if (g_fram_state != APP_FRAM_STATE_WAIT_PROBE)
      {
        app_fram_fail("probe completed in an invalid state");
        return;
      }
      app_uart_printf("[FRAM] probe PASS (0x50 and 0x51)\r\n");
      g_fram_state = APP_FRAM_STATE_START_WRITE;
      break;

    case APP_OPERATION_WRITE:
      if (g_fram_state != APP_FRAM_STATE_WAIT_WRITE)
      {
        app_fram_fail("write completed in an invalid state");
        return;
      }
      app_uart_printf("[FRAM] async write PASS\r\n");
      g_fram_state = APP_FRAM_STATE_START_READ;
      break;

    case APP_OPERATION_READ:
      if (g_fram_state != APP_FRAM_STATE_WAIT_READ)
      {
        app_fram_fail("read completed in an invalid state");
        return;
      }

      if (memcmp(g_fram_readback, g_fram_test_pattern,
                 sizeof(g_fram_test_pattern)) != 0)
      {
        app_fram_fail("readback data mismatch");
        return;
      }

      g_fram_state = APP_FRAM_STATE_PASS;
      app_uart_printf("[FRAM] async read/compare PASS\r\n");
      app_uart_printf("[FRAM] ASYNC SELF-TEST PASS\r\n");
      break;

    default:
      app_fram_fail("unknown operation completion");
      break;
  }
}

static void app_fram_test_tick(uint64_t now_us)
{
#if APP_ENABLE_FRAM_SELF_TEST
  StorageIoSubmitResult submit_result;
  StorageOperationToken token;
  uint64_t deadline_us = now_us + APP_FRAM_OPERATION_TIMEOUT_US;

  switch (g_fram_state)
  {
    case APP_FRAM_STATE_START_PROBE:
      token = app_make_token(APP_OPERATION_PROBE);
      g_expected_operation_id = APP_OPERATION_PROBE;
      g_fram_state = APP_FRAM_STATE_WAIT_PROBE;
      submit_result = fram_probe_async(&g_fram, token, deadline_us);
      if (submit_result != STORAGE_IO_SUBMIT_ACCEPTED)
      {
        app_uart_printf("[FRAM] probe submit rejected: %d\r\n",
                        (int)submit_result);
        app_fram_fail("could not submit probe");
      }
      break;

    case APP_FRAM_STATE_START_WRITE:
      token = app_make_token(APP_OPERATION_WRITE);
      g_expected_operation_id = APP_OPERATION_WRITE;
      g_fram_state = APP_FRAM_STATE_WAIT_WRITE;
      submit_result = fram_write_async(&g_fram,
                                       APP_FRAM_TEST_ADDRESS,
                                       g_fram_test_pattern,
                                       APP_FRAM_TEST_LENGTH,
                                       token,
                                       deadline_us);
      if (submit_result != STORAGE_IO_SUBMIT_ACCEPTED)
      {
        app_uart_printf("[FRAM] write submit rejected: %d\r\n",
                        (int)submit_result);
        app_fram_fail("could not submit write");
      }
      break;

    case APP_FRAM_STATE_START_READ:
      memset(g_fram_readback, 0, sizeof(g_fram_readback));
      token = app_make_token(APP_OPERATION_READ);
      g_expected_operation_id = APP_OPERATION_READ;
      g_fram_state = APP_FRAM_STATE_WAIT_READ;
      submit_result = fram_read_async(&g_fram,
                                      APP_FRAM_TEST_ADDRESS,
                                      g_fram_readback,
                                      APP_FRAM_TEST_LENGTH,
                                      token,
                                      deadline_us);
      if (submit_result != STORAGE_IO_SUBMIT_ACCEPTED)
      {
        app_uart_printf("[FRAM] read submit rejected: %d\r\n",
                        (int)submit_result);
        app_fram_fail("could not submit read");
      }
      break;

    case APP_FRAM_STATE_IDLE:
    case APP_FRAM_STATE_WAIT_PROBE:
    case APP_FRAM_STATE_WAIT_WRITE:
    case APP_FRAM_STATE_WAIT_READ:
    case APP_FRAM_STATE_PASS:
    case APP_FRAM_STATE_FAIL:
    default:
      break;
  }
#else
  (void)now_us;
#endif
}

static bool app_runtime_init(void)
{
  const FramConfig fram_config = {
    .client_id = APP_FRAM_CLIENT_ID,
    .slave_address_base_7bit = APP_FRAM_BASE_ADDRESS_7BIT,
    .capacity_bytes = FM24CL04B_SIZE_BYTES,
    .max_chunk_bytes = FM24CL04B_MAX_CHUNK_BYTES,
    .bus_priority = APP_FRAM_BUS_PRIORITY
  };

  /* PB8 low disables write protection for the FM24CL04B. */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);

  i2c_bus_init(&g_i2c_bus, NULL);

  if (!stm32_i2c1_hal_init(&g_i2c1_hal, &hi2c1, &g_i2c1_adapter))
  {
    app_uart_printf("[INIT] stm32_i2c1_hal_init failed\r\n");
    return false;
  }

  if (i2c_port_stm32_init(&g_i2c1_adapter,
                          &g_i2c1_hal,
                          stm32_i2c1_hal_ops(),
                          app_i2c_completion_sink,
                          &g_i2c_bus,
                          &g_i2c1_port) != PORT_OK)
  {
    app_uart_printf("[INIT] i2c_port_stm32_init failed\r\n");
    return false;
  }

  if (!i2c_bus_bind_port(&g_i2c_bus, &g_i2c1_port))
  {
    app_uart_printf("[INIT] i2c_bus_bind_port failed\r\n");
    return false;
  }

  if (fram_init(&g_fram, &g_i2c_bus, &fram_config) !=
      STORAGE_IO_SUBMIT_ACCEPTED)
  {
    app_uart_printf("[INIT] fram_init failed\r\n");
    return false;
  }

  if (!fram_make_storage_port(&g_fram, &g_fram_port) ||
      !g_fram_port.bind_completion(g_fram_port.context,
                                   app_fram_completion,
                                   NULL))
  {
    app_uart_printf("[INIT] F-RAM StoragePort binding failed\r\n");
    return false;
  }

#if APP_ENABLE_FRAM_SELF_TEST
  g_fram_state = APP_FRAM_STATE_START_PROBE;
  app_uart_printf("\r\n[INIT] Async I2C/F-RAM runtime ready\r\n");
  app_uart_printf("[FRAM] self-test will overwrite address %u..%u\r\n",
                  (unsigned int)APP_FRAM_TEST_ADDRESS,
                  (unsigned int)(APP_FRAM_TEST_ADDRESS +
                                 APP_FRAM_TEST_LENGTH - 1u));
#else
  g_fram_state = APP_FRAM_STATE_IDLE;
  app_uart_printf("\r\n[INIT] Async I2C/F-RAM runtime ready; self-test disabled\r\n");
#endif

  return true;
}

static void app_runtime_poll(uint64_t now_us)
{
  /* Deliver IRQ-latched completion before evaluating the deadline. */
  stm32_i2c1_hal_poll(&g_i2c1_hal);

  /* A timeout may synchronously notify the F-RAM driver in main context. */
  (void)i2c_bus_tick(&g_i2c_bus, now_us);

  app_fram_test_tick(now_us);
}
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
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */
  if (!app_runtime_init())
  {
    Error_Handler();
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    app_runtime_poll(app_now_us());
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
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable MSI Auto calibration
  */
  HAL_RCCEx_EnableMSIPLLMode();
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
  hi2c1.Init.Timing = 0x00000003;
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
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);

  /*Configure GPIO pin : PB8 */
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
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
  (void)file;
  (void)line;
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
