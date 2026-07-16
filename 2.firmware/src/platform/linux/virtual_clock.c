#define _POSIX_C_SOURCE 199309L
#include "platform/include/linux_virtual_clock.h"
#include "platform/include/virtual_clock.h"
#include "platform/include/monotonic_clock_port.h"
#include <time.h>
#include <string.h>


void linux_clock_init(LinuxVirtualClock *clock, LinuxClockMode mode)
{
    memset(clock, 0, sizeof(*clock));
    clock->mode = mode;
    clock->boot_generation = 1;
    clock->wall_time_valid = false;
}

void linux_clock_reset(LinuxVirtualClock *clock, bool preserve_wall)
{
    clock->now_us = 0;
    clock->boot_origin_us = 0;
    clock->boot_generation++;

    if (!preserve_wall) {
        clock->wall_base_s = 0;
        clock->wall_subsecond_us = 0;
        clock->time_generation = 0;
        clock->wall_time_valid = false;
    }
}

uint64_t linux_clock_now_us(const LinuxVirtualClock *clock)
{
    if (!clock) return 0;

    if (clock->mode == LINUX_CLOCK_MODE_DETERMINISTIC)
        return clock->now_us;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000);
}

void linux_clock_advance_by(LinuxVirtualClock *clock, uint64_t delta_us)
{
    if (!clock) return;
    if (clock->mode != LINUX_CLOCK_MODE_DETERMINISTIC) return;
    clock->now_us += delta_us;
}

bool linux_clock_advance_to(LinuxVirtualClock *clock, uint64_t target_us)
{
    if (!clock) return false;
    if (clock->mode != LINUX_CLOCK_MODE_DETERMINISTIC) return false;
    if (target_us < clock->now_us) return false;
    clock->now_us = target_us;
    return true;
}

void linux_clock_set_wall(LinuxVirtualClock *clock,
                          int64_t wall_s, uint32_t subsecond_us, bool valid)
{
    if (!clock) return;
    clock->wall_base_s = wall_s;
    clock->wall_subsecond_us = subsecond_us;
    clock->wall_time_valid = valid;
    clock->time_generation++;
}

/* Legacy API wrappers — share a single global clock instance */
static LinuxVirtualClock legacy_clock;
static bool legacy_clock_inited = false;

static void ensure_legacy_clock(void)
{
    if (!legacy_clock_inited) {
        linux_clock_init(&legacy_clock, LINUX_CLOCK_MODE_DETERMINISTIC);
        legacy_clock_inited = true;
    }
}

void virtual_clock_set_mode(int mode)
{
    ensure_legacy_clock();
    legacy_clock.mode = (LinuxClockMode)mode;
    /* Also set monotonic_now_us mode */
}

void virtual_clock_set(uint64_t now_us)
{
    ensure_legacy_clock();
    legacy_clock.now_us = now_us;
    legacy_clock.mode = LINUX_CLOCK_MODE_DETERMINISTIC;
}

void virtual_clock_advance(uint64_t delta_us)
{
    ensure_legacy_clock();
    legacy_clock.now_us += delta_us;
    legacy_clock.mode = LINUX_CLOCK_MODE_DETERMINISTIC;
}

/* monotonic_now_us() — delegates to the shared legacy clock */
uint64_t monotonic_now_us(void)
{
    ensure_legacy_clock();
    return linux_clock_now_us(&legacy_clock);
}
