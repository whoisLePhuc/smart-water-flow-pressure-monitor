# CMake Target Dependency Graph

**Created**: 2026-07-16
**Phase**: Phase 2 (CMake Boundaries)

## Target Hierarchy

```text
fw_domain_common (INTERFACE)
├── fw_domain_measurement (INTERFACE)
├── fw_domain_product (INTERFACE)
├── fw_domain_power (INTERFACE)
├── fw_domain_system (INTERFACE)
└── fw_domain_connectivity (INTERFACE)

core (INTERFACE — compatibility)

fw_infra_event (STATIC)
├── fw_infra_repository (STATIC)
├── fw_infra_time (STATIC)
└── infrastructure (STATIC — compatibility, wraps event+repo+time+bus+numeric)

fw_protocol_telemetry (INTERFACE)
└── fw_protocol_storage (INTERFACE)

app (STATIC)
└── depends on: infrastructure

drivers (STATIC)
└── depends on: infrastructure

fw_service_measurement (STATIC)
├── fw_service_processing (STATIC)
├── fw_service_leak (STATIC)
├── fw_service_storage (STATIC)
├── fw_service_connectivity (STATIC)
└── services (STATIC — compatibility, wraps all fw_service_* + drivers)

platform_linux (STATIC)
├── simulation (STATIC)
├── apps/linux_sim (executable)
└── tests/ (42 test executables)
```

## Layer Diagram

```
Application Layer:        app, linux_sim
Service Layer:            fw_service_measurement, fw_service_processing, 
                          fw_service_leak, fw_service_storage, fw_service_connectivity
Domain Layer:             fw_domain_common, fw_domain_measurement, fw_domain_product,
                          fw_domain_power, fw_domain_system, fw_domain_connectivity
Infrastructure Layer:     fw_infra_event, fw_infra_repository, fw_infra_time, core
Protocol Layer:           fw_protocol_telemetry, fw_protocol_storage
Driver Layer:             drivers
Platform Layer:           platform_linux, simulation
```

## Dependency Direction

| Target | Depends On |
|--------|-----------|
| domain/* | domain/common, project_compiler_options |
| fw_infra_event | core, project_compiler_options |
| fw_infra_repository | core, project_compiler_options |
| fw_infra_time | core, project_compiler_options |
| app | infrastructure |
| drivers | infrastructure |
| services/* | fw_infra_event, fw_infra_time, fw_infra_repository, app |
| platform_linux | core |
| simulation | platform_linux, app, infrastructure, services, drivers |

## Notes

- `FIRMWARE_INCLUDE_DIRS` reduced from 14 to 11 entries (removed infra sub-dirs)
- All domain targets are INTERFACE (header-only) until Phase 3 populates them
- Infrastructure sub-targets are STATIC; compatibility target wraps them + bus/numeric
- Service sub-targets are STATIC; compatibility target wraps them + drivers
- Platform and simulation targets remain monolithic (restructured in Phase 6)
