/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : STM32 platform bootstrap for the portable application
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdarg.h>
#include <stdio.h>

#include "app/app_composition.h"
#include "platform/stm32/adapters/i2c_port_stm32.h"
#include "platform/stm32/stm32_i2c1_hal.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_FRAM_CLIENT_ID             1u
#define APP_FRAM_BASE_ADDRESS_7BIT     0x50u
#define APP_FRAM_BUS_PRIORITY          3u
#define APP_STORAGE_IO_TIMEOUT_US      250000u
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
/* STM32-specific platform objects stay outside AppComposition. */
static Stm32I2c1Hal g_i2c1_hal;
static Stm32I2cAdapter g_i2c1_adapter;
static I2cPort g_i2c1_port;

/* The only composition root for portable infrastructure, drivers and services. */
static AppComposition g_app;

static uint32_t g_unmatched_i2c_completion_count;
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
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/**
 * @brief Extends the 32-bit HAL millisecond tick to monotonic 64-bit time.
 *
 * The clock has one-millisecond resolution but is expressed in microseconds to
 * satisfy the portable deadline contracts.
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

/**
 * @brief Routes the deferred STM32 adapter completion into AppComposition.
 */
static void app_i2c_completion_sink(void *context,
                                    const I2cPortRequest *request,
                                    PortStatus result)
{
  AppComposition *app = (AppComposition *)context;

  if (!app_composition_on_i2c_port_completion(app, request, result))
  {
    g_unmatched_i2c_completion_count++;
  }
}

/**
 * @brief Creates platform ports, then asks AppComposition to build the system.
 */
static bool app_runtime_init(void)
{
  const AppCompositionDependencies dependencies = {
    .shared_i2c_port = &g_i2c1_port,
    .fram_config = {
      .client_id = APP_FRAM_CLIENT_ID,
      .slave_address_base_7bit = APP_FRAM_BASE_ADDRESS_7BIT,
      .capacity_bytes = FM24CL04B_SIZE_BYTES,
      .max_chunk_bytes = FM24CL04B_MAX_CHUNK_BYTES,
      .bus_priority = APP_FRAM_BUS_PRIORITY
    },
    .storage_io_timeout_us = APP_STORAGE_IO_TIMEOUT_US
  };

  /* PB8 low disables write protection for the FM24CL04B. */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);

  /* MX_I2C1_Init() has already initialized hi2c1 at this point. */
  if (!stm32_i2c1_hal_init(&g_i2c1_hal,
                           &hi2c1,
                           &g_i2c1_adapter))
  {
    app_uart_printf("[INIT] stm32_i2c1_hal_init failed\r\n");
    return false;
  }

  if (i2c_port_stm32_init(&g_i2c1_adapter,
                          &g_i2c1_hal,
                          stm32_i2c1_hal_ops(),
                          app_i2c_completion_sink,
                          &g_app,
                          &g_i2c1_port) != PORT_OK)
  {
    app_uart_printf("[INIT] i2c_port_stm32_init failed\r\n");
    return false;
  }

  /*
   * The adapter stores &g_app as its callback context. app_composition_init()
   * may clear the object, but its address stays stable for the whole runtime.
   */
  if (!app_composition_init(&g_app, &dependencies))
  {
    app_uart_printf("[INIT] app_composition_init failed\r\n");
    return false;
  }

  if (!app_composition_start(&g_app))
  {
    app_uart_printf("[BOOT] volume restore start failed\r\n");
    return false;
  }

  app_uart_printf("\r\n[INIT] AppComposition initialized\r\n");
  app_uart_printf("[INIT] I2C1 -> shared bus -> F-RAM -> StorageService\r\n");
  app_uart_printf("[BOOT] volume restore started\r\n");
  return true;
}

static void app_runtime_poll(uint64_t now_us)
{
  /* Deliver IRQ-latched completion before evaluating portable deadlines. */
  stm32_i2c1_hal_poll(&g_i2c1_hal);

  /* Run every portable manager, driver and service owned by the root. */
  app_composition_poll(&g_app, now_us);
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
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

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

  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

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

  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

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

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);

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
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  (void)file;
  (void)line;
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
