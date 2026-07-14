---
document_id: FW-README-001
title: Firmware Design Documentation Index
status: DRAFT
version: 0.1
owner: Firmware
last_updated: 2026-07-14
source_of_truth: false
related_decisions:
  - DEC-HW-001
  - DEC-MEAS-001
  - DEC-MEAS-002
  - DEC-LEAK-001
  - DEC-SCHED-001
  - DEC-SCHED-002
  - DEC-SCHED-003
  - DEC-SCHED-004
  - DEC-ARCH-007
  - DEC-ARCH-008
related_documents:
  - ../../01_system_overview.md
  - ../../04_main_operation_flow.md
  - ../../06_system_fsm.md
  - ../../07_operating_modes.md
  - ../../08_data_flow.md
  - ../../09_error_handling_overview.md
  - ../../10_system_interfaces.md
  - ../../11_firmware_implication.md
  - ../../12_system_traceability.md
  - ../../13_reporting_and_connectivity_policy.md
  - ../../00_open_questions_and_decisions.md
  - ../../glossary.md
---

# 03 — Firmware Design

## 1. Mục đích

Thư mục `03_firmware` chứa bộ tài liệu thiết kế firmware của dự án **Smart Water Flow and Pressure Monitor**. Bộ tài liệu này chuyển các yêu cầu và quyết định ở mức hệ thống thành kiến trúc, contract, data model, state machine, timing rule và tiêu chí kiểm thử đủ rõ để:

- triển khai firmware mô phỏng trên Linux;
- kiểm thử application logic và service logic độc lập phần cứng;
- tích hợp các emulator của peripheral;
- chuyển cùng codebase sang STM32L433RCT6 thông qua platform abstraction;
- duy trì traceability từ requirement và decision tới module, interface, code và test;
- hỗ trợ review kỹ thuật trong quá trình tài liệu và code được phát triển song song.

README này là tài liệu điều hướng. Nó không thay thế source-of-truth của system design, hardware, communication, algorithm hoặc simulation.

---

## 2. Firmware baseline

Baseline firmware hiện tại:

| Hạng mục | Baseline |
|---|---|
| Main MCU | STM32L433RCT6 |
| Ultrasonic measurement | MAX35103 và ultrasonic transducers |
| Temperature | MAX35103 measurement subsystem |
| Pressure measurement | Resistive pressure bridge theo product variant + ZSSC3241 qua I2C |
| Persistent storage | FM24CL04B, fixed partition và A/B record |
| Local configuration | nRF52810 custom BLE coprocessor qua UART/AT riêng |
| Remote telemetry | Quectel EC200U-CN qua UART/AT riêng |
| Timekeeping | STM32 internal RTC + monotonic timebase |
| Local display | Segment LCD; mapping/interface chi tiết theo hardware source-of-truth |
| Runtime | Bare-metal cooperative event loop; RTOS optional later |
| Low power | STOP 2 capable; wake policy theo hardware và firmware contract |
| Development model | Simulation-first trên Linux, sau đó port platform backend sang STM32 |

Các đặc điểm baseline bắt buộc:

- measurement, processing, storage, BLE, 4G và LCD là các logical service; không mặc định là RTOS task;
- ISR và hardware callback chỉ capture trạng thái tối thiểu và phát event;
- application/service action phải bounded và non-blocking;
- measurement scheduling dùng monotonic time và độc lập wall-clock reporting;
- MAX35103 dùng event-timing mode theo measurement contract;
- production pressure acquisition dùng ZSSC3241 one-shot, hoàn tất bất đồng bộ bằng EOC hoặc bounded status polling;
- BLE chỉ tạo request, command hoặc `PendingConfig`; BLE không sở hữu `ActiveConfig` và không truy cập trực tiếp sensor/storage;
- 4G chỉ gửi immutable telemetry record được tạo từ runtime data đã publish;
- MVP tạo telemetry theo scheduled slot; leak transition không tự phát immediate telemetry;
- LCD chỉ đọc dữ liệu đã publish;
- persistent write chỉ đi qua `StorageService`;
- firmware dùng profile theo `firmware variant + device calibration + bounded runtime configuration`;
- OTA, generic remote command và remote configuration qua 4G không thuộc baseline hiện tại.

---

## 3. Phạm vi của bộ tài liệu

### 3.1. Trong phạm vi

Bộ tài liệu firmware định nghĩa:

- runtime model, event loop, event catalog và scheduling policy;
- firmware architecture, dependency direction và module responsibility;
- system FSM binding xuống application/service behavior;
- data model, unit, ownership, atomic publication và freshness;
- measurement cycle cho flow, temperature và pressure;
- integration contract cho MAX35103 và ZSSC3241;
- signal processing, flow computation, calibration và volume accumulation;
- sensor profile, firmware variant và per-device calibration contract;
- leak detection implementation contract;
- config transaction, persistent storage và runtime repository;
- telemetry queue, diagnostic log và runtime snapshot;
- BLE, 4G, LCD và factory/service integration ở phía firmware;
- error handling, health monitoring, watchdog, boot/self-check và low-power;
- platform abstraction cho Linux và STM32;
- interrupt, DMA và callback rule;
- build strategy, implementation plan, test strategy và porting plan;
- firmware traceability và các mục `NEEDS_VERIFICATION`.

### 3.2. Ngoài phạm vi

Bộ tài liệu này không sở hữu hoặc định nghĩa lại:

- mục tiêu sản phẩm và system requirement tổng thể;
- schematic, pin mapping, electrical constraint hoặc power budget phần cứng;
- công thức nguyên lý và bằng chứng nghiên cứu thuật toán ở mức system/principle;
- BLE GATT schema, UART application frame hoặc custom AT command chính thức;
- telemetry JSON schema, MQTT/HTTP server contract hoặc modem AT command set chính thức;
- emulator socket protocol, scenario format hoặc fault-injection protocol chính thức;
- chi tiết register không được xác nhận từ datasheet;
- OTA hoặc remote control ngoài baseline;
- production qualification value chưa được kiểm chứng bằng hardware/dataset.

Nếu firmware cần sử dụng một contract thuộc nhóm trên, tài liệu firmware phải dẫn chiếu source-of-truth và chỉ mô tả binding/implementation.

---

## 4. Source-of-truth

| Nội dung | Source-of-truth | Vai trò của `03_firmware` |
|---|---|---|
| System goal, operating flow, system FSM | Nhóm `00_overview` | Ánh xạ thành runtime/module behavior |
| Nguyên lý flow, pressure, temperature, leak | `1.docs/01_principle/` | Thiết kế implementation và test contract |
| Component, pin, peripheral, electrical constraint | `1.docs/02_hardware/` | Mapping driver và STM32 platform |
| Firmware architecture và runtime contract | `1.docs/03_firmware/` | Source-of-truth |
| BLE/4G protocol và payload contract | `1.docs/04_communication/` | Binding sang service/repository |
| Persistent layout chính thức | Tài liệu storage được chấp thuận | Codec, commit và recovery implementation |
| Emulator, virtual time, scenario và CI contract | `1.docs/08_simulation/` | Mapping Linux platform/backend |
| Decision status | `00_open_questions_and_decisions.md` | Hiện thực các decision đã accepted |
| Thuật ngữ | `glossary.md` | Sử dụng nhất quán, không định nghĩa lại tùy ý |

Trong cấu trúc hiện tại, các tài liệu overview đang nằm tại project root nhưng vẫn được xem là logical document group `00_overview`.

Quy tắc source-of-truth:

1. Một contract chỉ có một nơi sở hữu chính thức.
2. Tài liệu binding không được âm thầm thay đổi contract nguồn.
3. Nếu hai tài liệu mâu thuẫn, phải tạo hoặc cập nhật decision trước khi implement.
4. Giá trị chưa được qualification phải ghi `NEEDS_VERIFICATION`, không được biến thành default production một cách suy đoán.

---

## 5. Runtime và kiến trúc tổng thể

Firmware sử dụng kiến trúc phân lớp:

```text
Application
  -> Services
    -> Protocol / Binding / Repository contracts
      -> Device Drivers
        -> Platform Abstraction
          -> Linux backend hoặc STM32 backend
```

Các nguyên tắc dependency:

- dependency chỉ đi từ layer cao xuống layer thấp hoặc qua interface được công bố;
- core algorithm không phụ thuộc Linux, POSIX, STM32 HAL hoặc RTOS API;
- application không gọi trực tiếp device driver;
- communication service không đọc trực tiếp sensor;
- display không đọc trực tiếp sensor;
- callback truyền thông không ghi trực tiếp persistent storage;
- driver không chứa business rule, leak rule hoặc product scheduling policy;
- Linux socket/emulator protocol chỉ xuất hiện trong Linux backend;
- STM32 HAL, DMA và IRQ chỉ xuất hiện trong STM32 backend/driver adapter;
- `DataRepository`, `ConfigRepository` và `StorageService` có owner boundary rõ ràng.

Runtime flow tổng quát:

```text
Collect events
  -> dispatch application/service FSM
  -> run bounded service steps
  -> publish final RuntimeSnapshot when required
  -> process deferred storage/display/communication work
  -> evaluate safe low-power entry
```

---

## 6. Luồng dữ liệu chuẩn

### 6.1. Measurement

```text
Monotonic scheduler / device event
  -> MeasurementManager
  -> MAX35103 and/or ZSSC3241 driver
  -> raw measurement
  -> validation and quality evaluation
  -> processing, calibration and compensation
  -> flow / temperature / pressure result
  -> volume and leak processing
  -> one atomic RuntimeSnapshot publication
```

Flow, temperature và pressure có thể có period khác nhau. Mỗi result phải có timestamp, validity, freshness, production acceptance và reason metadata phù hợp.

### 6.2. BLE configuration

```text
BLE client
  -> nRF52810
  -> BLE UART/AT transport
  -> BleConfigService
  -> frame, permission and range validation
  -> PendingConfig
  -> persistent commit when required
  -> ActiveConfig version replacement
  -> ConfigApplyRequest
  -> APPLIED / DEFERRED / REJECTED
  -> aggregate response
```

### 6.3. Scheduled telemetry

```text
RTC/time event
  -> TimeService
  -> ReportingScheduler
  -> REPORT_DUE
  -> latest acceptable RuntimeSnapshot
  -> immutable TelemetryRecord
  -> RAM TelemetryQueue
  -> CellularTelemetryService
  -> EC200U-CN
  -> server acknowledgement
```

Trong MVP, `TelemetryQueue` là FIFO tĩnh trong RAM, tối đa 64 record và có TTL 24 giờ. Queue không được persist vào FM24CL04B; mất queue khi reset/brownout là accepted limitation cho tới khi có decision mới.

### 6.4. LCD

```text
RuntimeSnapshot publication / display refresh event
  -> LcdService
  -> display view model
  -> LCD driver
  -> segment mapping
```

---

## 7. Cấu trúc tài liệu

```text
03_firmware/
├── README.md
├── 00_core/
├── 10_measurement/
├── 20_data_and_storage/
├── 30_interfaces/
├── 40_reliability/
├── 50_platform/
└── 90_implementation/
```

### 7.1. `00_core`

| File | Mục đích |
|---|---|
| `00_runtime_decision.md` | Runtime model và invariant |
| `01_firmware_architecture.md` | Layer, module, responsibility và dependency |
| `02_event_model_and_scheduler.md` | Event catalog, priority, monotonic scheduler và timeout |
| `03_system_fsm_binding.md` | Binding system FSM sang application/service FSM |
| `04_data_model_and_ownership.md` | Kiểu dữ liệu, unit, owner, consumer và publication |

### 7.2. `10_measurement`

| File | Mục đích |
|---|---|
| `10_measurement_cycle.md` | Chu kỳ measurement end-to-end |
| `11_max35103_integration.md` | MAX35103 driver/service contract |
| `12_pressure_measurement_zssc3241.md` | Pressure acquisition và ZSSC3241 contract |
| `13_signal_processing.md` | Validation, filtering và quality processing |
| `14_flow_computation.md` | Flow computation contract |
| `15_calibration_algorithm.md` | Calibration pipeline và profile application |
| `16_sensor_profile_and_variant.md` | Variant/profile/calibration/runtime config contract |
| `17_leak_detection.md` | Leak evidence/state implementation |
| `18_volume_accumulation.md` | Forward/reverse/net volume và checkpoint trigger |

### 7.3. `20_data_and_storage`

| File | Mục đích |
|---|---|
| `20_runtime_snapshot.md` | Snapshot schema, publication và reader contract |
| `21_config_management.md` | Pending/active/apply/rollback transaction |
| `22_persistent_storage.md` | Partition, record, A/B commit, CRC và recovery |
| `23_telemetry_queue.md` | Queue ownership, capacity, retry và drop policy |
| `24_event_and_diagnostic_log.md` | Structured event/error record và diagnostic counters |

### 7.4. `30_interfaces`

| File | Mục đích |
|---|---|
| `30_ble_integration.md` | BLE service và nRF52810 transport integration |
| `31_ble_command_and_config_binding.md` | Command/config binding tới firmware services |
| `32_4g_modem_integration.md` | EC200U-CN asynchronous modem FSM |
| `33_telemetry_payload_binding.md` | Runtime/record binding sang payload contract |
| `34_lcd_display_integration.md` | LCD view model, refresh và segment binding |
| `35_factory_service_interface.md` | Factory, service và calibration access contract |

### 7.5. `40_reliability`

| File | Mục đích |
|---|---|
| `40_error_detection_and_recovery.md` | Error taxonomy, escalation và recovery |
| `41_health_monitor.md` | Health evidence, counters và readiness |
| `42_watchdog_strategy.md` | Watchdog ownership, liveness và reset evidence |
| `43_low_power_mode.md` | STOP 2 admission, wake source và resume |
| `44_boot_and_self_check.md` | Boot order, readiness, self-check và degraded startup |

### 7.6. `50_platform`

| File | Mục đích |
|---|---|
| `50_platform_abstraction.md` | Portable platform API và behavioral contract |
| `51_linux_platform_backend.md` | Linux implementation và emulator connection |
| `52_stm32_platform_backend.md` | STM32 HAL/DMA/IRQ mapping |
| `53_interrupt_dma_and_callback_rules.md` | ISR/callback boundary và concurrency rule |

### 7.7. `90_implementation`

| File | Mục đích |
|---|---|
| `90_firmware_implementation_plan.md` | Phase, vertical slice và Definition of Done |
| `91_build_and_variant_strategy.md` | CMake, target, feature và product variant |
| `92_firmware_test_strategy.md` | Unit, contract, integration và system test |
| `93_linux_simulation_integration.md` | Firmware-to-emulator integration contract |
| `94_linux_to_stm32_porting_plan.md` | Porting order, bring-up và equivalence verification |
| `95_firmware_traceability.md` | Requirement/decision-to-design/code/test mapping |

---

## 8. Thứ tự đọc và triển khai

### 8.1. Foundation

```text
README.md
  -> 00_runtime_decision.md
  -> 01_firmware_architecture.md
  -> 04_data_model_and_ownership.md
  -> 02_event_model_and_scheduler.md
  -> 03_system_fsm_binding.md
```

### 8.2. Linux simulation foundation

```text
50_platform_abstraction.md
  -> 51_linux_platform_backend.md
  -> 53_interrupt_dma_and_callback_rules.md
  -> 90_firmware_implementation_plan.md
  -> 91_build_and_variant_strategy.md
  -> 92_firmware_test_strategy.md
  -> 93_linux_simulation_integration.md
```

### 8.3. First vertical slice

```text
10_measurement_cycle.md
  -> 11_max35103_integration.md
  -> 12_pressure_measurement_zssc3241.md
  -> 13_signal_processing.md
  -> 16_sensor_profile_and_variant.md
  -> 20_runtime_snapshot.md
```

Mục tiêu vertical slice đầu tiên:

```text
Linux boot
  -> monotonic scheduler
  -> MAX35103/ZSSC3241 emulator
  -> portable driver
  -> measurement validation
  -> atomic RuntimeSnapshot publication
```

Các nhóm processing nâng cao, storage, LCD, BLE, 4G và reliability được bổ sung sau khi slice này chạy deterministic trong test.

---

## 9. Architecture decisions áp dụng

| Decision | Trạng thái áp dụng |
|---|---|
| Bare-metal cooperative event loop; RTOS optional later | Baseline |
| Monotonic measurement scheduler | Baseline |
| MAX35103 event-timing mode | Baseline |
| Pressure profile theo variant + per-device calibration + bounded runtime config | Baseline |
| ZSSC3241 one-shot pressure acquisition | Baseline |
| Pressure trend là supporting evidence, không tự đổi leak state trong MVP | Baseline |
| Config apply dùng versioned request/result và safe boundary | Baseline |
| Scheduled-only telemetry cho MVP | Baseline |
| Hai reporting window với start/interval configurable | Baseline |
| BLE và 4G dùng UART riêng, service riêng | Baseline |
| 4G không cung cấp OTA/generic remote command trong baseline | Baseline |
| Persistent data dùng fixed partition, version, CRC và A/B record | Baseline |
| Telemetry queue là RAM-only trong MVP | Accepted limitation |
| Linux simulation-first, STM32 platform backend sau | Development baseline |

Decision ID và trạng thái chính thức phải lấy từ `00_open_questions_and_decisions.md`. Bảng này chỉ tóm tắt ảnh hưởng lên firmware.

---

## 10. Quy tắc viết tài liệu

Tài liệu thiết kế module sử dụng template chung:

```markdown
---
document_id: FW-XXX-NNN
title: Tên tài liệu
status: DRAFT
version: 0.1
owner: Firmware
last_updated: YYYY-MM-DD
source_of_truth: false
related_decisions: []
related_documents: []
---

# Tên tài liệu

## 1. Mục đích

## 2. Phạm vi

## 3. Source-of-truth và tài liệu liên quan

## 4. Requirement/decision được hiện thực

## 5. Trách nhiệm

## 6. Ngoài phạm vi

## 7. Interface và dependency

## 8. Data model và đơn vị

## 9. State machine hoặc sequence

## 10. Timing, timeout và non-blocking behavior

## 11. Configuration

## 12. Error detection và recovery

## 13. Linux simulation mapping

## 14. STM32 mapping

## 15. Test và acceptance criteria

## 16. Traceability

## 17. Open issues / NEEDS_VERIFICATION

## 18. Revision history

| Version | Date | Change | Author |
|---|---|---|---|
| 0.1 | YYYY-MM-DD | Initial draft | Firmware |
```

Quy tắc sử dụng:

- giữ thứ tự section để người và AI truy xuất nhất quán;
- mục không áp dụng phải ghi `N/A` và lý do;
- không điền nội dung suy đoán để tránh section trống;
- requirement phải có ID hoặc đường dẫn nguồn;
- timeout/range chưa chốt phải ghi `NEEDS_VERIFICATION`;
- interface phải chỉ rõ owner, caller, input, output và error;
- data field phải có type, unit, scale, range và validity khi áp dụng;
- test phải bao gồm normal, boundary, invalid, timeout và recovery path;
- mọi thay đổi contract phải cập nhật revision history và traceability.

README, implementation plan, build strategy, test strategy, porting plan và traceability có thể dùng cấu trúc chuyên biệt phù hợp mục đích thay vì ép đủ 18 section.

---

## 11. Metadata và vòng đời tài liệu

Mỗi tài liệu phải có YAML metadata tối thiểu:

```yaml
document_id: FW-XXX-NNN
title: Document title
status: DRAFT
version: 0.1
owner: Firmware
last_updated: YYYY-MM-DD
source_of_truth: false
related_decisions: []
related_documents: []
```

Vòng đời tài liệu:

```text
DRAFT
  -> IN_REVIEW
  -> ACCEPTED
  -> IMPLEMENTED
  -> VERIFIED
  -> SUPERSEDED
```

Ý nghĩa:

- `DRAFT`: đang xây dựng, contract có thể thay đổi;
- `IN_REVIEW`: đã đủ nội dung để review;
- `ACCEPTED`: thiết kế được chấp thuận để implement;
- `IMPLEMENTED`: code tương ứng đã tồn tại và test cơ bản đạt;
- `VERIFIED`: acceptance/traceability đã được xác minh;
- `SUPERSEDED`: được thay thế bởi version hoặc tài liệu khác.

`source_of_truth: true` chỉ dùng khi tài liệu thực sự sở hữu contract. Binding, summary và README thường không phải source-of-truth cho contract bên ngoài phạm vi của chúng.

---

## 12. Quy ước tên firmware

| Loại | Quy ước | Ví dụ |
|---|---|---|
| Logical service trong tài liệu | PascalCase | `MeasurementManager` |
| Source/header file | lower_snake_case | `measurement_manager.c` |
| Public API | Module prefix + verb | `MeasurementManager_Run()` |
| Enum/state/event | UPPER_SNAKE_CASE | `MEAS_PHASE_WAIT_RESULT` |
| Unit suffix trong field | lower_snake_case suffix | `pressure_pa`, `timestamp_ms` |
| Document ID | `FW-<GROUP>-<NUMBER>` | `FW-CORE-001` |
| Requirement | ID từ source-of-truth | `SYS-REQ-...` |
| Decision | ID từ decision registry | `DEC-MEAS-001` |

Không dùng hậu tố `Task` nếu module chưa phải RTOS task. Không dùng cùng một tên cho snapshot version, schema version, config version và firmware version.

---

## 13. Phát triển tài liệu và code song song

Workflow chuẩn cho mỗi vertical slice:

```text
Resolve requirement and decision
  -> write or update design contract
  -> define public interface and data model
  -> define acceptance tests
  -> implement portable code
  -> implement Linux adapter/emulator integration
  -> run unit/contract/integration tests
  -> update document from verified behavior
  -> update traceability
```

Architecture và shared contract phải đi trước code một bước. Module document và code có thể phát triển song song sau khi dependency, ownership và public interface đã đủ ổn định.

Definition of Done cho một slice:

- requirement và decision đã được dẫn chiếu;
- responsibility và ngoài phạm vi rõ ràng;
- public interface và data ownership ổn định;
- code tuân thủ dependency rule;
- normal, boundary, timeout, invalid và recovery test đạt;
- Linux integration chạy deterministic;
- tài liệu phản ánh behavior đã kiểm chứng;
- traceability được cập nhật;
- phần chưa kiểm chứng trên hardware được đánh dấu `NEEDS_VERIFICATION`.

---

## 14. Tài liệu tham chiếu chính

- `../../01_system_overview.md`
- `../../04_main_operation_flow.md`
- `../../06_system_fsm.md`
- `../../07_operating_modes.md`
- `../../08_data_flow.md`
- `../../09_error_handling_overview.md`
- `../../10_system_interfaces.md`
- `../../11_firmware_implication.md`
- `../../12_system_traceability.md`
- `../../13_reporting_and_connectivity_policy.md`
- `../../00_open_questions_and_decisions.md`
- `../../glossary.md`
- `../01_principle/`
- `../02_hardware/`
- `../04_communication/`
- `../08_simulation/`
- STM32L433RCT6 datasheet và reference manual
- MAX35103 datasheet
- ZSSC3241 datasheet
- FM24CL04B datasheet
- nRF52810 product specification
- Quectel EC200U-CN hardware/AT documentation

---

## 15. Trạng thái hiện tại

| Nhóm | Trạng thái ban đầu |
|---|---|
| `README.md` | DRAFT |
| `00_core` | Chưa triển khai |
| `10_measurement` | Chưa triển khai |
| `20_data_and_storage` | Chưa triển khai |
| `30_interfaces` | Chưa triển khai |
| `40_reliability` | Chưa triển khai |
| `50_platform` | Chưa triển khai |
| `90_implementation` | Chưa triển khai |

Bộ tài liệu nên bắt đầu với toàn bộ `00_core`, sau đó chốt `50_platform` và measurement vertical slice đầu tiên.

---

## 16. Revision history

| Version | Date | Change | Author |
|---|---|---|---|
| 0.1 | 2026-07-14 | Initial firmware documentation index and baseline | Firmware |
