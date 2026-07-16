#include "reporting_schedule.h"
#include <string.h>
#include <stdio.h>

/* Default schedule: W0=06:00/15min, W1=22:00/5min */
#define DEFAULT_W0_START   (6u * 60u)    /* 360 */
#define DEFAULT_W0_INTERVAL 15u
#define DEFAULT_W1_START   (22u * 60u)   /* 1320 */
#define DEFAULT_W1_INTERVAL 5u

/* Seconds per minute */
#define SECS_PER_MIN 60LL

void ReportingSchedule_Init(ReportingSchedule *rs)
{
    memset(rs, 0, sizeof(*rs));
    rs->cfg.schedule_version = 1;
    rs->cfg.schedule_generation = 1;
    rs->cfg.utc_offset_minutes = 420;  /* VN UTC+07:00 */
    rs->cfg.windows[0].start_minute = DEFAULT_W0_START;
    rs->cfg.windows[0].interval_minutes = DEFAULT_W0_INTERVAL;
    rs->cfg.windows[1].start_minute = DEFAULT_W1_START;
    rs->cfg.windows[1].interval_minutes = DEFAULT_W1_INTERVAL;
    rs->generation = 1;
}

bool RS_ValidateConfig(const ReportingScheduleConfig *cfg, char *err, uint16_t err_len)
{
    if (!cfg) { if (err && err_len) snprintf(err, err_len, "null"); return false; }
    if (cfg->schedule_version == 0) { if (err && err_len) snprintf(err, err_len, "version zero"); return false; }

    for (int i = 0; i < RS_NUM_WINDOWS; i++) {
        if (cfg->windows[i].start_minute > 1439) {
            if (err && err_len) snprintf(err, err_len, "W%d start > 1439", i);
            return false;
        }
        if (cfg->windows[i].interval_minutes < RS_INTERVAL_MIN ||
            cfg->windows[i].interval_minutes > RS_INTERVAL_MAX) {
            if (err && err_len) snprintf(err, err_len, "W%d interval out of 5-60", i);
            return false;
        }
    }

    /* No duplicate start minutes */
    if (cfg->windows[0].start_minute == cfg->windows[1].start_minute) {
        if (err && err_len)
            snprintf(err, err_len, "duplicate start");
        return false;
    }

    /* Minimum window duration */
    uint16_t w0s = cfg->windows[0].start_minute;
    uint16_t w1s = cfg->windows[1].start_minute;

    uint16_t dur;
    if (w1s > w0s) {
        dur = (uint16_t)(w1s - w0s);
    } else {
        dur = (uint16_t)(1440u - (uint32_t)w0s + (uint32_t)w1s);
    }
    if (dur < RS_MIN_WINDOW_MIN) {
        if (err && err_len) snprintf(err, err_len, "window < 30min");
        return false;
    }

    return true;
}

/* Get active window index at given wall time (seconds since epoch) */
uint8_t RS_GetActiveWindow(const ReportingScheduleConfig *cfg, int64_t wall_s)
{
    int64_t day_s = wall_s % 86400LL;
    if (day_s < 0) day_s += 86400LL;
    uint16_t minute = (uint16_t)(day_s / 60LL);

    uint16_t w0 = cfg->windows[0].start_minute;
    uint16_t w1 = cfg->windows[1].start_minute;

    if (w1 > w0) {
        if (minute >= w0 && minute < w1) return 0;
        return 1;
    } else {
        if (minute >= w0 || minute < w1) return 0;
        return 1;
    }
}

/* Get slot ordinal for a given window and wall time */
uint16_t RS_GetSlotOrdinal(const ReportingScheduleConfig *cfg, uint8_t win_id, int64_t wall_s)
{
    if (win_id >= RS_NUM_WINDOWS) return 0;
    int64_t day_s = wall_s % 86400LL;
    if (day_s < 0) day_s += 86400LL;
    uint16_t minute = (uint16_t)(day_s / 60LL);
    uint16_t start = cfg->windows[win_id].start_minute;
    uint16_t interval = cfg->windows[win_id].interval_minutes;
    if (interval == 0) return 0;

    int16_t diff;
    if (minute >= start) {
        diff = (int16_t)(minute - start);
    } else {
        diff = (int16_t)(1440 + minute - start);
    }
    return (uint16_t)(diff / interval);
}

/* Get wall time for a specific slot in a window */
int64_t RS_GetSlotDueWall(const ReportingScheduleConfig *cfg, uint8_t win_id, uint16_t ordinal)
{
    if (win_id >= RS_NUM_WINDOWS) return 0;
    uint16_t start = cfg->windows[win_id].start_minute;
    uint16_t interval = cfg->windows[win_id].interval_minutes;
    return (start + ordinal * interval) * SECS_PER_MIN;
}

/* Get next due wall time at or after given wall time */
int64_t RS_GetNextDue(const ReportingScheduleConfig *cfg, int64_t wall_s)
{
    int64_t day_start = (wall_s / 86400LL) * 86400LL;
    int64_t earliest = wall_s + 86400LL;  /* max bound */

    for (int w = 0; w < RS_NUM_WINDOWS; w++) {
        uint16_t start = cfg->windows[w].start_minute;
        uint16_t interval = cfg->windows[w].interval_minutes;
        if (interval == 0) continue;

        int64_t first_slot_s = day_start + start * SECS_PER_MIN;
        if (first_slot_s >= wall_s) {
            if (first_slot_s < earliest) earliest = first_slot_s;
        } else {
            /* Compute next slot after wall_s */
            int64_t elapsed = wall_s - first_slot_s;
            uint16_t ordinal = (uint16_t)(elapsed / (interval * SECS_PER_MIN)) + 1;
            int64_t next_s = first_slot_s + ordinal * interval * SECS_PER_MIN;
            if (next_s < earliest) earliest = next_s;
        }
    }

    /* Check if already past today — if so, try tomorrow */
    if (earliest >= wall_s + 86400LL) {
        int64_t tomorrow = day_start + 86400LL;
        for (int w = 0; w < RS_NUM_WINDOWS; w++) {
            uint16_t start = cfg->windows[w].start_minute;
            if (tomorrow + start * SECS_PER_MIN < earliest)
                earliest = tomorrow + start * SECS_PER_MIN;
        }
    }

    return earliest;
}

/* Slot dedup */
bool RS_IsSlotAccepted(const ReportingSchedule *rs, const ReportSlotIdentity *slot)
{
    if (!rs || !slot) return false;
    if (!rs->has_accepted) return false;
    return rs->last_accepted.schedule_version == slot->schedule_version
        && rs->last_accepted.window_id == slot->window_id
        && rs->last_accepted.slot_due_wall_s == slot->slot_due_wall_s;
}

void RS_MarkSlotAccepted(ReportingSchedule *rs, const ReportSlotIdentity *slot)
{
    if (!rs || !slot) return;
    rs->last_accepted = *slot;
    rs->has_accepted = true;
}

bool RS_ApplyConfig(ReportingSchedule *rs, const ReportingScheduleConfig *cfg)
{
    if (!rs || !cfg) return false;
    if (!RS_ValidateConfig(cfg, NULL, 0)) return false;

    rs->cfg = *cfg;
    rs->generation++;
    rs->has_accepted = false;
    return true;
}
