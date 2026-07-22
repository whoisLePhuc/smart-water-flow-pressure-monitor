#include "main.h"

#include "support/stm32_test_platform.h"
#include "support/stm32_test_reporter_uart.h"
#include "support/stm32_test_runner.h"
#include "test_groups.h"

I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart2;

static Stm32TestPlatform s_platform;
static Stm32TestUartReporter s_uart_reporter;
static Stm32TestRunner s_runner;

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);

static uint64_t test_now_us(void)
{
    static uint32_t previous_ms;
    static uint64_t epoch_ms;
    const uint32_t current_ms = HAL_GetTick();
    if (current_ms < previous_ms)
        epoch_ms += (1ull << 32u);
    previous_ms = current_ms;
    return (epoch_ms + current_ms) * 1000ull;
}

int main(void)
{
    HAL_Init();
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_I2C1_Init();

    /* Current board revision connects FM24CL04B WP to PB8; low enables write. */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);

    Stm32TestReporter reporter;
    if (!stm32_test_uart_reporter_init(&s_uart_reporter, &huart2,
                                        100u, &reporter) ||
        !stm32_test_platform_init(&s_platform, &hi2c1))
        Error_Handler();

    fram_test_groups_bind_platform(&s_platform);
    size_t group_count = 0u;
    const Stm32TestGroup *groups = fram_test_groups(&group_count);
    stm32_test_runner_init(&s_runner, groups, group_count, &reporter);

    while (1) {
        stm32_test_runner_poll(&s_runner, test_now_us());
        if (stm32_test_runner_finished(&s_runner))
            __WFI();
    }
}

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clock = {0};

    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
        Error_Handler();
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

    oscillator.OscillatorType = RCC_OSCILLATORTYPE_LSE |
                                RCC_OSCILLATORTYPE_MSI;
    oscillator.LSEState = RCC_LSE_ON;
    oscillator.MSIState = RCC_MSI_ON;
    oscillator.MSICalibrationValue = 0u;
    oscillator.MSIClockRange = RCC_MSIRANGE_6;
    oscillator.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK)
        Error_Handler();

    clock.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                      RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clock.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
    clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock.APB1CLKDivider = RCC_HCLK_DIV1;
    clock.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_0) != HAL_OK)
        Error_Handler();
    HAL_RCCEx_EnableMSIPLLMode();
}

static void MX_I2C1_Init(void)
{
    hi2c1.Instance = I2C1;
    hi2c1.Init.Timing = 0x00000003;
    hi2c1.Init.OwnAddress1 = 0u;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0u;
    hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK ||
        HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK ||
        HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0u) != HAL_OK)
        Error_Handler();
}

static void MX_USART2_UART_Init(void)
{
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
        Error_Handler();
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
    gpio.Pin = GPIO_PIN_8;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
}
#endif
