---
document_id: FW-IMPL-095
title: Firmware Traceability
status: ACTIVE
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-ARCH-001
  - DEC-ARCH-005
  - DEC-ARCH-006
  - DEC-ARCH-007
  - DEC-DATA-003
  - DEC-HW-007
---

# Firmware Traceability

## 0. Baseline và quy ước

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- `Implemented`: có source + composition/use path + acceptance test.
- `Foundation`: component/algorithm có source/test nhưng chưa end-to-end.
- `Partial`: có một phần binding/backend.
- `Planned`: normative contract, chưa có source/test đầy đủ.
- Architecture check tại baseline: PASS, 0 error/warning.
- CMake/CTest chưa được chạy lại trong môi trường document refactor vì executable `cmake` không có.

## 1. Capability matrix

| Capability | Decisions/requirements | Source | Test | Status |
|---|---|---|---|---|
| Cooperative runtime | DEC-SYS-006, DEC-MEAS-001 | event/queue/time/app loop | scheduler/event/determinism | Implemented core |
| System FSM | DEC-SYS-007, DEC-MODE-001 | `src/app/system_fsm.*`, mode_guard | FSM unit | Partial binding |
| Transactional snapshot | DEC-DATA-003, DEC-ARCH-006, FW-SNAP-* | repositories | repository/txn unit | Implemented |
| Generic measurement registry | DEC-ARCH-001, FW-MEAS-* | measurement_manager | manager unit | Implemented framework |
| MAX acquisition | DEC-MEAS-002 | MAX driver + Linux peer | scenarios MAX | Partial skeleton |
| ZSSC acquisition | DEC-MEAS-003, DEC-HW-006 | ZSSC driver + peer | scenarios ZSSC | Partial skeleton |
| Flow/pressure/calibration | DEC-MEAS-004, DEC-ARCH-002/003 | processing/calibration | numeric/service unit | Foundation |
| Production measurement E2E | DEC-ARCH-001/002/003 | not fully bound | no complete raw→snapshot test | Partial |
| Leak | DEC-LEAK-001/002 | leak services | leak unit | Foundation/partial binding |
| Volume | DEC-DATA-001/002 | volume/checkpoint | unit + integration | Implemented algorithm |
| Power/battery | DEC-HW-005 OPEN, DEC-PWR-001 OPEN | power service/facade/ADC ports | unit + power E2E + ADC contract | Partial target |
| Storage codec/A-B | DEC-DATA-004/005 | protocols/storage, service, driver | codec/A-B/power-loss | Foundation |
| Storage canonical port path | DEC-ARCH-005, DEC-HW-006 | service direct FramDriver + separate port | fragmented tests | Partial |
| Telemetry queue/reporting | DEC-COM-002/003/004, DEC-SCHED-* | telemetry/connectivity | unit + reporting E2E | Implemented MVP logic |
| Modem transport | DEC-HW-003, DEC-COM-001 | no modem backend | none | Planned |
| BLE/config | DEC-HW-002, DEC-ARCH-007 | validators only | partial | Planned |
| LCD | DEC-HW-004 OPEN | none | none | Planned |
| Diagnostics | DEC-DIAG-001 OPEN, DEC-ERR-005 | statuses/counters only | indirect | Planned |
| Watchdog | DEC-ERR-004 | none | none | Planned |
| Low power | DEC-HW-007 | FSM foundation only | FSM/power tests | Partial |
| Linux simulation | simulation decisions/contracts | platform/linux + simulation | integration/system | Implemented |
| STM32 backend | platform requirements | synchronous ADC adapter only | ADC contract | Partial |

## 2. Document-to-code map

- Core: `00_core/*` → app/infrastructure/domain.
- Measurement: `10_measurement/*` → drivers, measurement, processing, calibration, leak, volume.
- Data: `20_data_and_storage/*` → repositories, storage protocol/service, connectivity queue.
- Interfaces: `30_interfaces/*` → mostly planned; telemetry service foundation only.
- Reliability: `40_reliability/*` → statuses/FSM foundations; dedicated services mostly planned.
- Platform: `50_platform/*` → ports, Linux backend, STM32 ADC adapter.
- Implementation: `90_implementation/*` → CMake/tests/scripts/simulation.

## 3. Open decision gates

| Decision | Effect |
|---|---|
| DEC-HW-004 | LCD model/interface/adapter release |
| DEC-HW-005 | Battery source/regulator/4G peak-current qualification |
| DEC-HW-008 | Dedicated service UART |
| DEC-PWR-001 | Battery threshold/hysteresis |
| DEC-DIAG-001 | Diagnostic retention/coalescing/upload |

Không tài liệu firmware nào được tự chốt numeric/physical behavior thuộc các gate này.

## 4. Change control

Mọi public API, event ID, schema, ownership, target graph hoặc status thay đổi phải cập nhật requirement mapping và test evidence trong cùng change. Không chuyển Planned/Foundation/Partial sang Implemented nếu thiếu composition binding hoặc acceptance test.


