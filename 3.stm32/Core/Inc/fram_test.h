/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : fram_test.h
  * @brief          : FRAM hardware test suite
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __FRAM_TEST_H
#define __FRAM_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
  * @brief  Run all FRAM hardware tests.
  *         Results are printed via UART2.
  * @retval 0 if all tests pass, 1 if any test fails
  */
int FRAM_Test_RunAll(void);

#ifdef __cplusplus
}
#endif

#endif /* __FRAM_TEST_H */
