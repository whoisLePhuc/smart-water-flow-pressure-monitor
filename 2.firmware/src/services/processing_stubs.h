#ifndef SWFPM_PROCESSING_STUBS_H
#define SWFPM_PROCESSING_STUBS_H

#include "event/data_model.h"

/* Deterministic processing stubs for measurement results.
 * Output provenance is ESTIMATED — not production-qualified.
 * Purpose, origin, provenance and binding are preserved from input metadata. */

/* Process MAX raw measurement into temperature + flow results */
TemperatureResult stub_process_temperature(const ResultMetadata *source_meta,
                                            int32_t raw_tof);

FlowResult stub_process_flow(const ResultMetadata *source_meta,
                              int32_t delta_tof,
                              FlowDirection direction);

/* Process ZSSC raw measurement into pressure result */
PressureResult stub_process_pressure(const ResultMetadata *source_meta,
                                      int32_t raw_count);

#endif
