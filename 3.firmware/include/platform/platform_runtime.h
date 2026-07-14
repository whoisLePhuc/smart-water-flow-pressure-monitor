#ifndef SWFPM_PLATFORM_RUNTIME_H
#define SWFPM_PLATFORM_RUNTIME_H

#include <stdbool.h>

/*
 * Platform runtime port
 *
 * Called once at boot (platform_init) and once per event-loop turn
 * (platform_poll). platform_poll() gives the platform a chance to
 * inject external events (emulator input, UART data, wake sources)
 * before the core event collection phase.
 *
 * Returns true if the platform has pending work that needs dispatch.
 */

void platform_init(void);
bool platform_poll(void);

#endif /* SWFPM_PLATFORM_RUNTIME_H */
