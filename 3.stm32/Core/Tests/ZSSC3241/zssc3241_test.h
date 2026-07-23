/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : zssc3241_test.h
  * @brief          : ZSSC3241 STM32 HAL hardware test suite
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __ZSSC3241_TEST_H
#define __ZSSC3241_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "zssc3241_stm32_hal.h"

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint8_t address_7bit;

    GPIO_TypeDef *reset_port;
    uint16_t reset_pin;
    bool reset_available;
    bool eoc_available;

    bool run_full_nvm_dump;
    bool run_cyclic_test;
    uint32_t cyclic_settle_ms;

    bool pressure_mapping_enabled;
    uint32_t pressure_code_min;
    uint32_t pressure_code_max;
    int32_t pressure_min_mbar;
    int32_t pressure_max_mbar;

    /* Optional; NULL selects ZSSC3241_DefaultConfig(). */
    const Zssc3241Config *driver_config;
} Zssc3241TestConfig;

/**
  * @brief  Run the complete non-destructive ZSSC3241 test suite.
  *         Results are printed through UART2. No NVM write command is issued.
  * @param  config Board-specific I2C, GPIO, and optional pressure mapping.
  * @retval 0 if all executed tests pass, 1 if any test fails.
  */
int ZSSC3241_Test_RunAll(const Zssc3241TestConfig *config);

/**
  * @brief Forward the ZSSC3241 EOC EXTI event while the suite is running.
  *
  * Example:
  *   void HAL_GPIO_EXTI_Callback(uint16_t pin)
  *   {
  *       if (pin == ZSSC_EOC_Pin) {
  *           ZSSC3241_Test_OnEoc();
  *       }
  *   }
  */
void ZSSC3241_Test_OnEoc(void);

#ifdef __cplusplus
}
#endif

#endif /* __ZSSC3241_TEST_H */