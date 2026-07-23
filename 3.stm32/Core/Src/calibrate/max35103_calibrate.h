/**
  ******************************************************************************
  * @file    max35103_calibrate.h
  * @brief   MAX35103 AutoCal board integration — extract from main.c
  *
  * Single-call Init() replaces ~500 lines of main.c boilerplate.
  * Call Poll() from the super-loop; it returns COMPLETE when done.
  ******************************************************************************
  */

#ifndef SWFPM_MAX35103_CALIBRATE_H
#define SWFPM_MAX35103_CALIBRATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "max35103.h"
#include "max35103_autocal.h"
#include "main.h"

/**
  * @brief  Initialise transport, driver, and AutoCal state machine.
  *
  *         hspi, nss_port/pin, reset_port/pin are the board-level SPI and
  *         reset GPIO.  huart receives diagnostic text; pass NULL for silent.
  *         When profile is NULL an internal default is used.
  *
  * @retval MAX35103_AUTOCAL_RUNNING on success.
  *         Any other value indicates a start-up failure.
  */
Max35103AutoCalStatus MAX35103_Calibrate_Init(
    SPI_HandleTypeDef *hspi,
    GPIO_TypeDef *nss_port, uint16_t nss_pin,
    GPIO_TypeDef *reset_port, uint16_t reset_pin,
    UART_HandleTypeDef *huart,
    const Max35103Profile *profile);

/**
  * @brief  Advance the AutoCal FSM by one step.  Call from the super-loop.
  *
  * @retval MAX35103_AUTOCAL_COMPLETE  — calibration finished, call GetReport
  * @retval MAX35103_AUTOCAL_RUNNING   — still in progress
  * @retval < MAX35103_AUTOCAL_OK      — fatal error
  */
Max35103AutoCalStatus MAX35103_Calibrate_Poll(void);

/** @brief  True while the calibration engine is active. */
bool MAX35103_Calibrate_IsActive(void);

#ifdef __cplusplus
}
#endif

#endif /* SWFPM_MAX35103_CALIBRATE_H */
