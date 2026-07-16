#ifndef SWFPM_REPORTING_SCHEDULE_H
#define SWFPM_REPORTING_SCHEDULE_H

#include <stdint.h>
#include <stdbool.h>
#include "domain/connectivity/reporting_types.h"
#include "domain/common/metadata.h"

#define RS_NUM_WINDOWS        2
#define RS_INTERVAL_MIN       5u
#define RS_INTERVAL_MAX       60u
#define RS_MIN_WINDOW_MIN     30u

typedef struct {
    ReportingScheduleConfig cfg;
    uint32_t generation;
    ReportSlotIdentity last_accepted;
    bool     has_accepted;
} ReportingSchedule;

void ReportingSchedule_Init(ReportingSchedule *rs);

bool RS_ValidateConfig(const ReportingScheduleConfig *cfg, char *err, uint16_t err_len);

uint16_t RS_GetSlotOrdinal(const ReportingScheduleConfig *cfg, uint8_t win_id, int64_t wall_s);
int64_t  RS_GetSlotDueWall(const ReportingScheduleConfig *cfg, uint8_t win_id, uint16_t ordinal);
int64_t  RS_GetNextDue(const ReportingScheduleConfig *cfg, int64_t wall_s);
uint8_t  RS_GetActiveWindow(const ReportingScheduleConfig *cfg, int64_t wall_s);

bool RS_IsSlotAccepted(const ReportingSchedule *rs, const ReportSlotIdentity *slot);
void RS_MarkSlotAccepted(ReportingSchedule *rs, const ReportSlotIdentity *slot);

bool RS_ApplyConfig(ReportingSchedule *rs, const ReportingScheduleConfig *cfg);

#endif
