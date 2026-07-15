#ifndef SWFPM_VIRTUAL_CLOCK_H
#define SWFPM_VIRTUAL_CLOCK_H

#include <stdint.h>

/* Legacy backward-compatible API — delegates to LinuxVirtualClock */
void virtual_clock_set_mode(int mode);
void virtual_clock_set(uint64_t now_us);
void virtual_clock_advance(uint64_t delta_us);

#define CLOCK_MODE_REAL    0
#define CLOCK_MODE_VIRTUAL 1

#endif
