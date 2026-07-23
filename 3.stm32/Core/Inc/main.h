/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32l4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
extern I2C_HandleTypeDef hi2c1;
extern UART_HandleTypeDef huart2;
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define BATTERY_SENSE_Pin GPIO_PIN_0
#define BATTERY_SENSE_GPIO_Port GPIOA
#define MAX_NSS_Pin GPIO_PIN_4
#define MAX_NSS_GPIO_Port GPIOA
#define MAX_SCK_Pin GPIO_PIN_5
#define MAX_SCK_GPIO_Port GPIOA
#define MAX_MISO_Pin GPIO_PIN_6
#define MAX_MISO_GPIO_Port GPIOA
#define MAX_MOSI_Pin GPIO_PIN_7
#define MAX_MOSI_GPIO_Port GPIOA
#define MAX_RST_Pin GPIO_PIN_4
#define MAX_RST_GPIO_Port GPIOC
#define MAX_INT_Pin GPIO_PIN_5
#define MAX_INT_GPIO_Port GPIOC
#define MAX_INT_EXTI_IRQn EXTI9_5_IRQn
#define MAX_CMP_Pin GPIO_PIN_0
#define MAX_CMP_GPIO_Port GPIOB
#define MAX_WDO_Pin GPIO_PIN_2
#define MAX_WDO_GPIO_Port GPIOB
#define MAX_WDO_EXTI_IRQn EXTI2_IRQn
#define MEASURE_PIN_Pin GPIO_PIN_9
#define MEASURE_PIN_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
