#include "services/processing_stubs.h"
#include <string.h>

static void set_stub_metadata(ResultMetadata *meta, const ResultMetadata *source)
{
    memset(meta, 0, sizeof(*meta));
    meta->purpose = source->purpose;
    meta->origin = source->origin;
    meta->provenance = PROVENANCE_ESTIMATED;
    meta->binding = source->binding;
    meta->source_generation = source->source_generation;
    meta->sample_sequence = source->sample_sequence;
    meta->sample_monotonic_us = source->sample_monotonic_us;
}

TemperatureResult stub_process_temperature(const ResultMetadata *source_meta,
                                            int32_t raw_tof)
{
    TemperatureResult r;
    memset(&r, 0, sizeof(r));
    set_stub_metadata(&r.meta, source_meta);

    /* Stub: raw_tof maps linearly to milli-degrees C.
     * NOT production accuracy — estimated only. */
    r.temperature_mdeg_c = (raw_tof / 10) * 25;
    r.meta.reason_flags = 0x0001; /* estimated/test-only */
    return r;
}

FlowResult stub_process_flow(const ResultMetadata *source_meta,
                              int32_t delta_tof,
                              FlowDirection direction)
{
    FlowResult r;
    memset(&r, 0, sizeof(r));
    set_stub_metadata(&r.meta, source_meta);

    /* Stub: delta_tof → microlitres/second */
    r.flow_ul_per_s = (int64_t)delta_tof * 10;
    r.direction = direction;
    r.meta.reason_flags = 0x0001;
    return r;
}

PressureResult stub_process_pressure(const ResultMetadata *source_meta,
                                      int32_t raw_count)
{
    PressureResult r;
    memset(&r, 0, sizeof(r));
    set_stub_metadata(&r.meta, source_meta);

    /* Stub: raw_count → Pascals */
    r.pressure_pa = raw_count * 5;
    r.meta.reason_flags = 0x0001;
    return r;
}
