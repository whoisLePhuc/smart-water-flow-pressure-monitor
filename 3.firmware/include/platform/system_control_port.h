#ifndef SWFPM_SYSTEM_CONTROL_PORT_H
#define SWFPM_SYSTEM_CONTROL_PORT_H

#include <stdint.h>

/*
 * System control port
 *
 * Platform-level system control operations. In simulation mode
 * (Linux), system_request_reset() logs the reason and terminates
 * the process. On STM32, it triggers a NVIC system reset.
 *
 * The reason code follows the firmware error taxonomy defined in
 * the reliability documents.
 */

void system_request_reset(uint32_t reason);

#endif /* SWFPM_SYSTEM_CONTROL_PORT_H */
