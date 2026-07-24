/**
 * @file autocal_board.h
 * @brief STM32 board integration for the MAX35103 auto-calibration service.
 */

#ifndef SWFPM_AUTOCAL_BOARD_H
#define SWFPM_AUTOCAL_BOARD_H

#include "max35103.h"
#include "max35103_autocal.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
/** Initialize and start auto-calibration with the board-specific backend. */
void AUTOCAL_Start(Max35103Driver *driver, const Max35103Profile *seed_profile);

/**
 * Advance auto-calibration by one non-blocking step.
 * @return MAX35103_AUTOCAL_RUNNING  still in progress
 *         MAX35103_AUTOCAL_COMPLETE calibration finished successfully
 *         < 0                       terminal error
 */
Max35103AutoCalStatus AUTOCAL_Poll(void);

/**
 * Copy the calibrated profile after AUTOCAL_Poll() returns COMPLETE.
 * @return true  profile copied
 *         false calibration not complete (nothing written)
 */
bool AUTOCAL_GetSelectedProfile(Max35103Profile *profile);

/**
 * Zero-flow offset measured during calibration [ps].
 * Valid only when AUTOCAL_Poll() returned COMPLETE.
 */
int64_t AUTOCAL_GetZeroFlowOffset(void);
#endif /* FIRMWARE_BUILD_MAX35103_AUTOCAL */

#ifdef __cplusplus
}
#endif

#endif /* SWFPM_AUTOCAL_BOARD_H */