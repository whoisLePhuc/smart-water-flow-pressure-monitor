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
 * Advance auto-calibration by one non-blocking step and emit diagnostics.
 *
 * @return MAX35103_AUTOCAL_RUNNING while active, MAX35103_AUTOCAL_COMPLETE
 *         after a verified profile is applied, or a negative terminal error.
 */
Max35103AutoCalStatus AUTOCAL_Poll(void);

/** Copy the completed report to caller-owned storage. */
bool AUTOCAL_GetReport(Max35103AutoCalReport *report);
#endif /* FIRMWARE_BUILD_MAX35103_AUTOCAL */

#ifdef __cplusplus
}
#endif

#endif /* SWFPM_AUTOCAL_BOARD_H */
