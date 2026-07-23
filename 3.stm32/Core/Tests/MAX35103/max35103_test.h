/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : max35103_test.h
  * @brief          : MAX35103 hardware test suite
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef MAX35103_TEST_H
#define MAX35103_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "max35103.h"

/**
  * @brief  Run all MAX35103 hardware and conversion tests.
  *         Results are printed via UART2. The profile must contain the real
  *         register image and RTD/reference-resistor values for this board.
  * @param  transport Initialized board transport; copied by each test driver.
  * @param  profile Production profile; it remains owned by the caller.
  * @retval 0 if all executed tests pass, 1 if any test fails
  *
  * Example:
  *   int result = MAX35103_Test_RunAll(
  *       &max35103_transport, &g_max35103_profile);
  */
int MAX35103_Test_RunAll(
    const Max35103Transport *transport,
    const Max35103Profile *profile);

#ifdef __cplusplus
}
#endif

#endif /* MAX35103_TEST_H */
