#ifndef SWFPM_SENSOR_PROFILE_H
#define SWFPM_SENSOR_PROFILE_H

#include <stdint.h>
#include <stdbool.h>

/* Profile identity and version */
typedef struct {
    uint32_t profile_id;
    uint32_t schema_version;
    uint32_t calibration_version;
    uint8_t  qualification_status; /* 0=test-only, 1=qualified */
} ProfileIdentity;

/* Temperature RTD profile */
typedef struct {
    ProfileIdentity id;
    int64_t  rtd_r0;             /* Reference resistance at 0°C, mΩ */
    uint16_t rtd_table_size;
    const int64_t *rtd_temp_table;   /* °C × 1000 */
    const int64_t *rtd_res_table;    /* mΩ */
} TemperatureProfile;

/* Flow geometry profile */
typedef struct {
    ProfileIdentity id;
    int64_t  pipe_area;          /* mm² × 1000 */
    int64_t  path_length;        /* mm × 1000 */
    int64_t  acoustic_velocity;  /* m/s × 1000 */
} FlowProfile;

/* Pressure sensor profile */
typedef struct {
    ProfileIdentity id;
    int32_t  pa_min;             /* Min rated pressure, Pa */
    int32_t  pa_max;             /* Max rated pressure, Pa */
    int32_t  application_min;    /* Application range min, Pa */
    int32_t  application_max;    /* Application range max, Pa */
    int64_t  endpoint_lo_raw;    /* Raw U24 at low endpoint */
    int64_t  endpoint_hi_raw;    /* Raw U24 at high endpoint */
    int32_t  endpoint_lo_pa;     /* Pressure at low endpoint, Pa */
    int32_t  endpoint_hi_pa;     /* Pressure at high endpoint, Pa */
    uint8_t  reference_type;     /* 0=absolute, 1=gauge, 2=differential */
} PressureProfile;

/* Calibration record */
typedef struct {
    uint32_t record_version;
    int64_t  gain;               /* Fixed-point gain */
    int64_t  offset;             /* Fixed-point offset */
    uint8_t  shift;              /* Right-shift for gain/offset */
} CalibrationRecord;

bool profile_validate_temperature(const TemperatureProfile *p, char *err, uint16_t err_max);
bool profile_validate_flow(const FlowProfile *p, char *err, uint16_t err_max);
bool profile_validate_pressure(const PressureProfile *p, char *err, uint16_t err_max);

#endif
