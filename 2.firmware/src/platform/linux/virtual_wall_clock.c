#include "virtual_wall_clock.h"
#include <string.h>

static struct {
    int64_t  wall_s;
    bool     continuity_valid;
    int64_t  alarm_s;
    bool     alarm_armed;
} s_wc;

void virtual_wall_clock_init(void)
{
    memset(&s_wc, 0, sizeof(s_wc));
    s_wc.wall_s = 0;
    s_wc.continuity_valid = true;
}

void virtual_wall_clock_set(int64_t wall_s)
{
    s_wc.wall_s = wall_s;
}

void virtual_wall_clock_advance(int64_t delta_s)
{
    s_wc.wall_s += delta_s;
}

int64_t virtual_wall_clock_now_s(void)
{
    return s_wc.wall_s;
}

void virtual_wall_clock_set_continuity(bool valid)
{
    s_wc.continuity_valid = valid;
}

bool virtual_wall_clock_get_continuity(void)
{
    return s_wc.continuity_valid;
}

void virtual_wall_clock_set_alarm(int64_t wall_s)
{
    s_wc.alarm_s = wall_s;
    s_wc.alarm_armed = true;
}

int64_t virtual_wall_clock_get_alarm(void)
{
    return s_wc.alarm_s;
}

bool virtual_wall_clock_is_alarm_triggered(void)
{
    if (!s_wc.alarm_armed) return false;
    if (s_wc.wall_s >= s_wc.alarm_s) {
        s_wc.alarm_armed = false;
        return true;
    }
    return false;
}
