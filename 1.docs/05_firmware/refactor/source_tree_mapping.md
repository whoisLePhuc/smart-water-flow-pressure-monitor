# Source Tree Mapping — Current → Target Structure

**Created**: 2026-07-16

| Old Path | New Path | Owner | Phase | Action |
|----------|----------|-------|-------|--------|
| `src/infrastructure/event/data_model.h` | `src/domain/common/metadata.h`, `src/domain/measurement/measurement_types.h`, `src/domain/product/volume_types.h`, `src/domain/product/leak_types.h`, `src/domain/system/system_types.h`, `src/domain/connectivity/reporting_types.h`, `src/domain/connectivity/delivery_types.h`, `src/infrastructure/event/event_id.h`, `src/infrastructure/repositories/runtime_snapshot.h` | Domain + Infra | P3 | Split |
| `src/infrastructure/repositories/data_repository.c` | `src/infrastructure/repositories/data_repository.c` (keep) + `src/infrastructure/repositories/repo_transaction.c` (new) | Infrastructure | P5 | Refactor |
| `src/infrastructure/event/app_event_router.c` | `src/infrastructure/event/event_mediator.c` | Infrastructure | P4 | Replace |
| `src/services/telemetry_builder.c` | `src/protocols/telemetry/telemetry_builder.c` | Protocols | P8 | Move |
| `src/services/storage_record.c` | `src/protocols/storage/storage_codec.c` | Protocols | P8 | Move |
| `src/services/measurement_manager.c` | `src/services/measurement/` (keep) | Services | P7 | Facade |
| `src/services/processing_stubs.c` | `src/services/processing/` (keep) | Services | P7 | Facade |
| `src/services/flow_service.c` | `src/services/measurement/` (keep) | Services | — | Keep |
| `src/services/pressure_service.c` | `src/services/measurement/` (keep) | Services | — | Keep |
| `src/services/leak_*.c` | `src/services/product/` (keep) | Services | — | Keep |
| `src/services/storage_service.c` | `src/services/storage/` (keep) | Services | P7 | Facade |
| `src/services/reporting_schedule.c` | `src/services/connectivity/` (keep) | Services | P7 | Facade |
| `src/services/cellular_delivery.c` | `src/services/connectivity/` (keep) | Services | P7 | Facade |
| `include/core/data_model.h` | — | — | P11 | Delete (replaced by domain headers) |
| `src/platform/include/monotonic_clock_port.h` | `src/ports/clock_port.h` | Ports | P6 | Move |
| `src/platform/include/system_control_port.h` | `src/ports/system_port.h` | Ports | P6 | Move |
| `src/platform/include/platform_runtime.h` | `src/ports/runtime_port.h` | Ports | P6 | Move |
| (new) `src/ports/adc_port.h` | `src/ports/adc_port.h` | Ports | P6 | New |
| (new) `src/ports/storage_port.h` | `src/ports/storage_port.h` | Ports | P6 | New |
