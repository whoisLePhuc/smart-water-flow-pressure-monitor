/**
  ******************************************************************************
  * @file    app_max35103.h
  * @brief   Application-level MAX35103 measurement state machine
  ******************************************************************************
  *
  * Owns the MAX35103 driver instance, seed profile and the
  * AUTOCAL -> MEASURING -> RECOVERING -> FAULT application state machine.
  * main.c only calls AppMax35103_Init() after peripheral init and
  * AppMax35103_Run() from the main loop.
  *
  ******************************************************************************
  */

#ifndef SWFPM_APP_MAX35103_H
#define SWFPM_APP_MAX35103_H

#include "max35103.h"
#include "max35103_stm32_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize the application state machine and start auto-calibration. */
void AppMax35103_Init(void);

/** Advance the application state machine by one non-blocking step. */
void AppMax35103_Run(void);

#ifdef __cplusplus
}
#endif

#endif /* SWFPM_APP_MAX35103_H */
