/**
 ******************************************************************************
 * @file    autocal_board.h
 * @brief   STM32 application adapter for the MAX35103 auto-calibration service
 ******************************************************************************
 *
 * @details
 * This module connects the portable search engine in max35103_autocal.c to
 * the concrete STM32 application composition.  It owns the singleton
 * calibration session used by the board, initializes the STM32 HAL transport,
 * starts the search, advances it from foreground context, emits UART
 * diagnostics, and retains the final evidence report for the application.
 *
 * The dependency direction is:
 *
 * @code
 * Application / main loop
 *          |
 *          v
 * autocal_board.c             Board policy, STM32 objects, UART diagnostics
 *          |
 *          v
 * max35103_autocal.c          Portable candidate search and validation
 *          |
 *          v
 * max35103.c + STM32 adapter  Device driver and physical transport
 * @endcode
 *
 * AUTOCAL_Start() performs board and service initialization.  After it
 * returns, the application must call AUTOCAL_Poll() repeatedly from normal
 * foreground or task context.  Each poll advances the portable state machine
 * by at most one measurement or one transition action, so the application
 * remains able to feed its watchdog and service unrelated work.
 *
 * @note This adapter is intentionally board-specific.  Portable calibration
 *       code must depend on max35103_autocal.h instead of this header.
 * @note The implementation uses one file-static session and is therefore not
 *       reentrant and does not support calibrating multiple devices at once.
 * @warning AUTOCAL_Start() calls Error_Handler() when mandatory board or
 *          initialization requirements fail.
 * @warning AUTOCAL_Poll() must not be called from an interrupt service routine;
 *          it may configure the device, execute SPI transfers, and write UART
 *          diagnostics.
 ******************************************************************************
 */

#ifndef SWFPM_AUTOCAL_BOARD_H
#define SWFPM_AUTOCAL_BOARD_H

#include "max35103.h"
#include "max35103_autocal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the board backend and start a new auto-calibration search.
 *
 * The function initializes the STM32 SPI transport, initializes and resets the
 * MAX35103 driver, builds the board's calibration policy, binds the driver to
 * the portable backend, initializes the caller-independent sample workspace,
 * and enters the DISCOVERY state.
 *
 * Both input objects are borrowed: the function does not take ownership of
 * either object.  The driver instance must remain valid for the entire
 * calibration session.  The seed profile is copied during initialization and
 * therefore need not remain valid after this function returns.
 *
 * @param[in,out] driver
 *     Driver instance used for all candidate configuration and measurement
 *     operations.  The instance is reinitialized by this function.
 * @param[in] seed_profile
 *     Known-safe starting profile.  Candidate zero preserves this entire
 *     profile before the generic discovery grid is evaluated.
 *
 * @pre STM32 HAL, @c hspi1, @c huart2, MAX35103 GPIOs, and the global STM32
 *      transport context declared by the application have been initialized.
 * @pre @p driver and @p seed_profile are non-NULL.
 * @post On success, AUTOCAL_Poll() returns MAX35103_AUTOCAL_RUNNING until the
 *       state machine reaches a terminal state.
 *
 * @note This function has no return value because board-level failures are
 *       reported through UART and Error_Handler().
 * @warning Starting a new session discards any previously retained report.
 */
void AUTOCAL_Start(Max35103Driver *driver, const Max35103Profile *seed_profile);

/**
 * @brief Advance the active search by one bounded state-machine step.
 *
 * The function calls MAX35103_AutoCalStep(), snapshots progress, and emits
 * structured UART records for candidate diagnostics, stage transitions,
 * retries, profile fallback, failure evidence, and the final profile.
 *
 * When calibration completes, the selected volatile profile is applied once
 * more through the normal driver before the report becomes available through
 * AUTOCAL_GetReport().
 *
 * @return MAX35103_AUTOCAL_RUNNING
 *     The search is active; call this function again from foreground context.
 * @return MAX35103_AUTOCAL_COMPLETE
 *     All validation stages passed and the selected profile was reapplied.
 * @return MAX35103_AUTOCAL_OK
 *     No calibration is active and no terminal error is retained.
 * @return MAX35103_AUTOCAL_DRIVER_ERROR
 *     A driver operation or final profile application failed.
 * @return MAX35103_AUTOCAL_NO_CANDIDATE
 *     All permitted retries and discovery-profile fallbacks were exhausted.
 * @return MAX35103_AUTOCAL_CANCELLED
 *     The portable search was cancelled.
 *
 * @note Calling this function after the session becomes inactive simply
 *       returns the retained terminal status.
 * @warning Call only from the same serialized context that owns the driver.
 */
Max35103AutoCalStatus AUTOCAL_Poll(void);

/**
 * @brief Copy the retained completed report to caller-owned storage.
 *
 * @param[out] report
 *     Destination for a complete value copy of Max35103AutoCalReport.
 *
 * @return true
 *     A verified report was available and copied successfully.
 * @return false
 *     @p report is NULL or the current/most recent session has no completed
 *     report.
 *
 * @note The copied report remains valid independently of subsequent calls to
 *       this module.
 */
bool AUTOCAL_GetReport(Max35103AutoCalReport *report);

#ifdef __cplusplus
}
#endif

#endif /* SWFPM_AUTOCAL_BOARD_H */