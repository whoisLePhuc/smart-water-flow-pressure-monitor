#include "services/sensor_profile.h"
#include "interpolation.h"
#include <string.h>
#include <stdio.h>

bool profile_validate_temperature(const TemperatureProfile *p, char *err, uint16_t err_max)
{
    if (!p) { if (err) snprintf(err, err_max, "NULL profile"); return false; }
    if (p->rtd_table_size < 2) { if (err) snprintf(err, err_max, "RTD table too small"); return false; }
    if (!table_is_monotonic(p->rtd_temp_table, p->rtd_res_table, p->rtd_table_size))
        { if (err) snprintf(err, err_max, "RTD table not monotonic"); return false; }
    return true;
}

bool profile_validate_flow(const FlowProfile *p, char *err, uint16_t err_max)
{
    if (!p) { if (err) snprintf(err, err_max, "NULL profile"); return false; }
    if (p->pipe_area <= 0) { if (err) snprintf(err, err_max, "Invalid pipe area"); return false; }
    if (p->path_length <= 0) { if (err) snprintf(err, err_max, "Invalid path length"); return false; }
    return true;
}

bool profile_validate_pressure(const PressureProfile *p, char *err, uint16_t err_max)
{
    if (!p) { if (err) snprintf(err, err_max, "NULL profile"); return false; }
    if (p->pa_max <= p->pa_min) { if (err) snprintf(err, err_max, "Invalid range"); return false; }
    if (p->endpoint_hi_raw <= p->endpoint_lo_raw)
        { if (err) snprintf(err, err_max, "Invalid endpoint raw"); return false; }
    return true;
}
