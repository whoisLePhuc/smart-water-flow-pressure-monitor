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

/*
 * Globals owned by this module. Kept external for the legacy HIL test path
 * in main.c and for the autocal board adapter (autocal_board.c).
 */
#if defined(FIRMWARE_BUILD_TESTS_MAX35103) || defined(FIRMWARE_BUILD_MAX35103_AUTOCAL)
extern Max35103Stm32HalContext g_max35103_hal_context;
extern Max35103Transport g_max35103_transport;
extern const Max35103Profile g_max35103_seed_profile;
#endif

#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
/** Initialize the application state machine and start auto-calibration. */
void AppMax35103_Init(void);

/** Advance the application state machine by one non-blocking step. */
void AppMax35103_Run(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* SWFPM_APP_MAX35103_H */
