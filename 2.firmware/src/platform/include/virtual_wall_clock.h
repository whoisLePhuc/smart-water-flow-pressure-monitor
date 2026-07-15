#ifndef SWFPM_VIRTUAL_WALL_CLOCK_H
#define SWFPM_VIRTUAL_WALL_CLOCK_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Virtual wall clock — independent domain from virtual monotonic clock.
 * Used for deterministic simulation of wall-clock/RTC behavior.
 * Portable code calls TimeService, not this clock directly.
 */

void virtual_wall_clock_init(void);
void virtual_wall_clock_set(int64_t wall_s);
void virtual_wall_clock_advance(int64_t delta_s);
int64_t virtual_wall_clock_now_s(void);

/* Simulate RTC continuity and alarm */
void virtual_wall_clock_set_continuity(bool valid);
bool virtual_wall_clock_get_continuity(void);
void virtual_wall_clock_set_alarm(int64_t wall_s);
int64_t virtual_wall_clock_get_alarm(void);
bool virtual_wall_clock_is_alarm_triggered(void);

#endif
