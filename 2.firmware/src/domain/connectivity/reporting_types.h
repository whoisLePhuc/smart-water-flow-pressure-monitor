#ifndef SWFPM_DOMAIN_REPORTING_TYPES_H
#define SWFPM_DOMAIN_REPORTING_TYPES_H

/* =================================================================
 * Domain: connectivity/reporting
 * Owner: domain/connectivity (fw_domain_connectivity)
 * ================================================================= */

#include <stdint.h>

/* ── Reporting window config ── */

typedef struct {
    uint16_t start_minute;       /* minute-of-day 0..1439 */
    uint16_t interval_minutes;   /* 5..60 */
} ReportingWindowConfig;

/* ── Reporting schedule config ── */

typedef struct {
    uint32_t schedule_version;
    uint32_t schedule_generation;
    int16_t  utc_offset_minutes;   /* VN = 420 */
    ReportingWindowConfig windows[2];
} ReportingScheduleConfig;

/* ── Report slot identity ── */

typedef struct {
    uint32_t schedule_version;
    uint8_t  window_id;           /* 0 or 1 */
    int64_t  slot_due_wall_s;     /* wall-clock time when slot is due */
} ReportSlotIdentity;

#endif /* SWFPM_DOMAIN_REPORTING_TYPES_H */
