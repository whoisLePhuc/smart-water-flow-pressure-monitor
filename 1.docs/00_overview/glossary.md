# Glossary — Smart Water Flow and Pressure Monitor

**Project:** Smart Water Flow and Pressure Monitor
**Document group:** `1.docs/00_overview`
**Document level:** System-level terminology and naming convention
**Role:** Source-of-truth for canonical terms and names
**Status:** Initial baseline

---

## 1. Mục tiêu

Tài liệu này định nghĩa thuật ngữ, tên logical service, tên firmware module, data object, event và identifier được sử dụng trong dự án **Smart Water Flow and Pressure Monitor**.

Mục tiêu chính:

* Giữ cách gọi nhất quán giữa overview, hardware, firmware, communication và simulation.
* Phân biệt hardware component, logical service và firmware implementation.
* Tránh dùng một thuật ngữ cho nhiều khái niệm khác nhau.
* Chuẩn hóa tên dữ liệu trong measurement, configuration, reporting và telemetry pipeline.
* Làm rõ thuật ngữ nào thuộc baseline và thuật ngữ nào còn `TBD` hoặc future scope.

Khi tài liệu khác sử dụng thuật ngữ khác với glossary, tài liệu đó phải ghi rõ mapping hoặc glossary phải được cập nhật trước.

---

## 2. Current Project Baseline Terms

| Thuật ngữ                               | Định nghĩa chuẩn                                                                                                                                                                                                                      |
| --------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Smart Water Flow and Pressure Monitor` | Tên đầy đủ của dự án giám sát lưu lượng, nhiệt độ, áp suất và rò rỉ; hỗ trợ BLE configuration, 4G telemetry, RTC scheduling và LCD display.                                                                                           |
| `SWFPM`                                 | Tên viết tắt nội bộ của `Smart Water Flow and Pressure Monitor`. Không bắt buộc dùng trong tên API.                                                                                                                                   |
| `STM32L433RCT6`                         | MCU điều phối trung tâm của baseline hiện tại.                                                                                                                                                                                        |
| `MAX35103`                              | Ultrasonic time-of-flight measurement IC dùng cùng cặp ultrasonic transducer. Không gọi MAX35103 là pressure sensor.                                                                                                                  |
| `Pressure bridge`                       | Phần tử cảm biến cầu điện trở chuyển áp suất nước thành tín hiệu analog. Model, reference type, dải đo và độ chính xác hiện là `TBD`.                                                                                                 |
| `ZSSC3241`                              | Sensor signal conditioner đã chọn cho pressure subsystem; conditioning, digitization và sensor-specific correction trước khi cung cấp pressure/status cho MCU qua I2C baseline. Không gọi ZSSC3241 là pressure transducer hoàn chỉnh. |
| `FM24CL04B`                             | F-RAM dùng cho persistent records nhỏ và quan trọng. Không mặc định dùng làm telemetry history dài hạn.                                                                                                                               |
| `BLE module`                            | Module Bluetooth Low Energy kết nối với MCU qua dedicated UART, dùng cho local configuration/service.                                                                                                                                 |
| `4G module`                             | Cellular modem/module kết nối với MCU qua dedicated UART, dùng để gửi telemetry lên server.                                                                                                                                           |
| `STM32 internal RTC`                    | RTC tích hợp trong MCU, cung cấp hardware timekeeping, alarm và wakeup event.                                                                                                                                                         |
| `LCD`                                   | Khối hiển thị cục bộ. Model và physical interface hiện là `TBD`.                                                                                                                                                                      |
| `Remote server`                         | Hệ thống ngoài nhận và xử lý telemetry do thiết bị gửi qua 4G.                                                                                                                                                                        |

Theo `DEC-ARCH-008`, những khối sau không thuộc baseline hiện tại và chỉ được bổ sung sau architecture/security review mới:

```text
Remote configuration through 4G
OTA firmware update
Valve or pump actuator
Long-term local telemetry history
Wi-Fi communication
RS485 or Modbus RTU
```

---

## 3. Naming Conventions

### 3.1. Logical service name

Dùng `PascalCase` cho logical service/module ở mức architecture:

```text
SystemManager
MeasurementManager
FlowComputationService
PressureMeasurementService
PressureProcessingService
CalibrationService
VolumeAccumulator
LeakDetectionService
DataRepository
ConfigRepository
StorageService
TimeService
ReportingScheduler
BleConfigService
CellularTelemetryService
LcdService
PowerManager
DiagnosticsService
HealthMonitor
CommandDispatcher
```

### 3.2. Firmware implementation name

Dùng `lower_snake_case` cho tên file/module C:

```text
system_manager.c
measurement_manager.c
flow_computation_service.c
pressure_measurement_service.c
pressure_processing_service.c
calibration_service.c
volume_accumulator.c
leak_detection_service.c
data_repository.c
config_repository.c
storage_service.c
time_service.c
reporting_scheduler.c
ble_config_service.c
cellular_telemetry_service.c
lcd_service.c
power_manager.c
diagnostics_service.c
health_monitor.c
command_dispatcher.c
```

### 3.3. Driver name

Dùng tên hardware/function boundary + hậu tố `_driver`:

```text
max35103_driver.c
pressure_sensor_driver.c
fram_driver.c
ble_uart_driver.c
cellular_uart_driver.c
rtc_driver.c
lcd_driver.c
power_monitor_driver.c
```

`RtcDriver` là hardware driver, không phải owner của reporting policy.

### 3.4. Event name

Dùng tiền tố `EVT_` và `UPPER_SNAKE_CASE` cho application event:

```text
EVT_MAX_RESULT_READY
EVT_PRESSURE_SAMPLE_DUE
EVT_PRESSURE_RESULT_READY
EVT_RTC_ALARM
EVT_REPORT_DUE
EVT_BLE_CONFIG_RECEIVED
EVT_CONFIG_COMMIT_REQUIRED
EVT_CONFIG_APPLY_STATUS
EVT_CONFIG_APPLIED
EVT_CELLULAR_TX_REQUESTED
EVT_CELLULAR_TX_COMPLETED
EVT_CELLULAR_TX_FAILED
EVT_LCD_REFRESH_DUE
EVT_STORAGE_COMMIT_REQUIRED
EVT_LEAK_DETECTED
EVT_ERROR_DETECTED
```

Event name mô tả điều đã xảy ra hoặc yêu cầu logic, không mô tả chi tiết cách HAL xử lý event.

### 3.5. Identifier prefixes

| Prefix            | Full name                    | Mục đích                                                                |
| ----------------- | ---------------------------- | ----------------------------------------------------------------------- |
| `SR-xxx`          | System Requirement           | Yêu cầu cấp hệ thống                                                    |
| `IF-xx`           | Physical/External Interface  | Interface hardware hoặc external boundary                               |
| `LIF-xx`          | Logical Interface            | Interface dữ liệu/hành vi giữa logical services                         |
| `OQ-xxx`          | Open Question                | Câu hỏi thiết kế chưa được chốt                                         |
| `OQ-<DOMAIN>-xxx` | Domain-scoped Open Question  | Open Question thuộc một nhóm cụ thể, ví dụ `OQ-TERM-001` trong glossary |
| `ADR-xxx`         | Architecture Decision Record | Quyết định kiến trúc đã được chốt và có rationale                       |
| `HW-xxx`          | Hardware Requirement         | Yêu cầu phần cứng chi tiết                                              |
| `FW-xxx`          | Firmware Requirement         | Yêu cầu firmware chi tiết                                               |
| `COM-xxx`         | Communication Requirement    | Yêu cầu protocol/communication                                          |
| `TC-xxx`          | Test Case                    | Test case dùng để validate requirement                                  |

### 3.6. Không dùng hậu tố `Task` khi chưa chốt RTOS

Baseline hiện tại sử dụng logical service và event-driven cooperative runtime. Vì vậy không dùng `Task` để đặt tên system-level nếu chưa có RTOS architecture decision.

| Nên dùng                   | Không nên dùng trong baseline hiện tại |
| -------------------------- | -------------------------------------- |
| `MeasurementManager`       | `MeasurementTask`                      |
| `BleConfigService`         | `BleTask`                              |
| `CellularTelemetryService` | `4GTask`                               |
| `StorageService`           | `StorageTask`                          |
| `ReportingScheduler`       | `ReportTask`                           |

Nếu sau này dùng RTOS, task mapping thuộc `03_firmware` và không thay đổi logical service names trong `00_overview`.

---

## 4. Hardware and Physical Measurement Terms

| Thuật ngữ                   | Viết tắt | Định nghĩa                                                                                                                                                                            |
| --------------------------- | -------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Ultrasonic transducer`     | -        | Phần tử chuyển đổi giữa tín hiệu điện và sóng siêu âm trong đường nước.                                                                                                               |
| `Transducer A/B`            | -        | Tên trung tính cho hai transducer ở hai phía measurement path.                                                                                                                        |
| `Water flow path`           | -        | Đường nước vật lý mà ultrasonic signal truyền qua và pressure sensor giám sát.                                                                                                        |
| `Transit-time principle`    | -        | Nguyên lý đo dựa trên chênh lệch thời gian truyền sóng siêu âm theo hai hướng.                                                                                                        |
| `Time-of-Flight`            | `ToF`    | Thời gian sóng siêu âm truyền giữa hai transducer.                                                                                                                                    |
| `Upstream ToF`              | `t_up`   | ToF theo hướng được quy ước là upstream.                                                                                                                                              |
| `Downstream ToF`            | `t_down` | ToF theo hướng được quy ước là downstream.                                                                                                                                            |
| `Differential transit time` | `Δt`     | Chênh lệch giữa upstream/downstream ToF, dùng làm đầu vào tính flow.                                                                                                                  |
| `Pressure`                  | `P`      | Áp suất nước tại vị trí pressure sensor. Đơn vị vật lý/canonical logical là pascal (`Pa`); runtime/telemetry scale phải được schema định nghĩa rõ.                                    |
| `Temperature`               | `T`      | Nhiệt độ đo được từ measurement subsystem, dùng cho compensation, diagnostics và telemetry. Canonical logical unit là `°C`; runtime baseline dùng signed millidegree Celsius (`m°C`). |
| `Measurement range`         | -        | Dải giá trị mà sensor/system được thiết kế để đo hợp lệ.                                                                                                                              |
| `Measurement accuracy`      | -        | Mức sai số cho phép của phép đo theo requirement đã chốt.                                                                                                                             |
| `Sample interval`           | -        | Khoảng thời gian giữa hai lần lấy mẫu của một measurement source.                                                                                                                     |
| `Conversion time`           | -        | Thời gian sensor/IC cần để hoàn thành một phép đo sau khi được trigger.                                                                                                               |

Quy tắc thuật ngữ:

* Dùng `MAX35103 measurement IC`, không dùng `MAX sensor` khi cần mô tả chính xác hardware role.
* Dùng `pressure bridge` cho sensing element và `ZSSC3241` cho signal conditioner; không gọi ZSSC3241 là pressure sensor hoàn chỉnh.
* Dùng `flow rate` cho lưu lượng tức thời và `volume` cho thể tích tích lũy.

---

## 5. Measurement Pipeline Terms

| Thuật ngữ chuẩn            | Định nghĩa                                                                                                                                                                                                                                                                 | Owner/Source                          |
| -------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------- |
| `RawUltrasonicMeasurement` | Dữ liệu thô đọc từ MAX35103, gồm ToF, temperature-related result, status và metadata.                                                                                                                                                                                      | `MeasurementManager`                  |
| `RawPressureMeasurement`   | Dữ liệu pressure/raw code và status đọc từ ZSSC3241 pressure subsystem, kèm timestamp/sequence/profile metadata.                                                                                                                                                           | `PressureMeasurementService`          |
| `ValidatedMeasurement`     | Measurement đã qua status check, range check và freshness check.                                                                                                                                                                                                           | Processing service tương ứng          |
| `ProcessedFlowMeasurement` | Kết quả flow đã qua kiểm tra và xử lý trước calibration cuối.                                                                                                                                                                                                              | `FlowComputationService`              |
| `CalibratedFlow`           | Flow đã áp dụng calibration và temperature compensation phù hợp.                                                                                                                                                                                                           | `CalibrationService`                  |
| `PressureResult`           | Áp suất đã validate, filter và calibration; kèm timestamp/quality.                                                                                                                                                                                                         | `PressureProcessingService`           |
| `TemperatureResult`        | Nhiệt độ đã convert, calibration và chuẩn hóa cho runtime use; là immutable/versioned data object độc lập.                                                                                                                                                                 | `CalibrationService`                  |
| `FlowResult`               | Data object của một lần xử lý flow, luôn kèm validity/quality và compensation status. Chỉ accepted production result mới được dùng cho volume, flow-based leak evidence và valid production telemetry; rejected result vẫn có thể được publish cho repository/diagnostics. | `CalibrationService`/`DataRepository` |
| `DEGRADED_NOT_ACCEPTED`    | Có kết quả sơ bộ hoặc metadata chẩn đoán nhưng không đạt điều kiện production; consumer không được dùng để cập nhật volume, tạo flow-based leak evidence hoặc báo cáo như measurement hợp lệ.                                                                              | Measurement quality policy            |
| `Uncompensated flow`       | Flow được tính khi temperature compensation không khả dụng. Baseline không chấp nhận loại result này cho production theo `DEC-ARCH-003`.                                                                                                                                   | `CalibrationService`                  |
| `VolumeState`              | Trạng thái tích lũy thể tích thuận, ngược hoặc net tùy requirement.                                                                                                                                                                                                        | `VolumeAccumulator`                   |
| `MeasurementCycle`         | Chuỗi hành động từ measurement event đến publish result.                                                                                                                                                                                                                   | `MeasurementManager`                  |
| `PRODUCTION_SAMPLE`        | Sample được tạo bởi production scheduler trong operating path hợp lệ và có thể đi tới product consumer khi validity/quality đạt yêu cầu. Không được tạo trong `SERVICE` baseline.                                                                                          | Production measurement pipeline       |
| `SERVICE_SAMPLE`           | Bounded sample được tạo trong authorized service session để kiểm tra/chẩn đoán; không được update production volume, leak state/evidence hoặc scheduled production telemetry.                                                                                              | Service measurement path              |
| `CALIBRATION_SAMPLE`       | Sample được tạo trong authorized calibration procedure để tính/verify calibration; không được đổi provenance hoặc đưa vào production consumer path.                                                                                                                        | Calibration service path              |
| `MeasurementQuality`       | Metadata đánh giá độ tin cậy của một result.                                                                                                                                                                                                                               | Processing/diagnostics                |
| `MeasurementTimestamp`     | Thời điểm measurement được tạo hoặc được xác nhận hoàn thành.                                                                                                                                                                                                              | `TimeService` + measurement pipeline  |

Pipeline flow chuẩn:

```text
MAX35103
  -> RawUltrasonicMeasurement
  -> validation
  -> FlowComputationService
  -> ProcessedFlowMeasurement
  -> CalibrationService
  -> CalibratedFlow / FlowResult
  -> VolumeAccumulator
  -> DataRepository
```

Pipeline pressure chuẩn:

```text
Pressure bridge + ZSSC3241
  -> RawPressureMeasurement
  -> validation
  -> PressureProcessingService
  -> PressureResult
  -> DataRepository
```

---

## 6. Flow, Volume and Calibration Terms

| Thuật ngữ                  | Ký hiệu | Định nghĩa                                                                                                                                                    |
| -------------------------- | ------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Flow velocity`            | `v`     | Vận tốc đại diện của dòng nước trong measurement path.                                                                                                        |
| `Volumetric flow rate`     | `Q`     | Thể tích nước đi qua trong một đơn vị thời gian. Đơn vị vật lý/canonical logical là `m³/s`; runtime/telemetry có thể dùng fixed-point scale được version hóa. |
| `Instantaneous flow`       | -       | Flow rate mới nhất đã validate và calibration.                                                                                                                |
| `Forward volume`           | -       | Tổng thể tích tích lũy theo hướng dòng chảy thuận đã quy ước; canonical logical unit là `m³`.                                                                 |
| `Reverse volume`           | -       | Tổng thể tích tích lũy theo hướng dòng chảy ngược đã quy ước; canonical logical unit là `m³`.                                                                 |
| `Net volume`               | -       | Hiệu có dấu giữa forward và reverse volume theo data model; canonical logical unit là `m³`.                                                                   |
| `Zero-flow offset`         | -       | Sai lệch measurement khi dòng chảy thực bằng hoặc gần bằng zero.                                                                                              |
| `K-factor`                 | `K`     | Hệ số calibration/chuyển đổi liên quan đến hình học, flow profile hoặc kết quả hiệu chuẩn.                                                                    |
| `Temperature compensation` | -       | Điều chỉnh measurement do ảnh hưởng của nhiệt độ.                                                                                                             |
| `Calibration profile`      | -       | Tập hệ số, offset, table hoặc metadata dùng để hiệu chỉnh measurement.                                                                                        |
| `Factory calibration`      | -       | Quy trình hiệu chuẩn trong sản xuất hoặc phòng thử nghiệm.                                                                                                    |
| `Field calibration`        | -       | Hiệu chỉnh hạn chế tại hiện trường theo policy được cho phép.                                                                                                 |

Không dùng `calibration` để chỉ filtering. Filtering giảm noise/biến động; calibration hiệu chỉnh systematic error hoặc chuyển đổi theo reference.

---

## 7. Pressure and Leak Detection Terms

| Thuật ngữ                | Định nghĩa                                                                                                                    |
| ------------------------ | ----------------------------------------------------------------------------------------------------------------------------- |
| `PressureResult`         | Kết quả áp suất đã validate, filter, calibration và gắn timestamp/quality.                                                    |
| `Pressure trend`         | Sự thay đổi của áp suất trong một khoảng thời gian xác định.                                                                  |
| `Pressure anomaly`       | Áp suất hoặc xu hướng áp suất vượt policy/threshold đã chốt.                                                                  |
| `Continuous flow`        | Dòng chảy tồn tại liên tục trong thời gian dài hơn threshold.                                                                 |
| `Unexpected flow`        | Dòng chảy xuất hiện ngoài pattern hoặc reporting/monitoring window được policy xem là bình thường.                            |
| `Leak detection`         | Quá trình phân tích dữ liệu flow, volume, time và pressure để phát hiện dấu hiệu rò rỉ.                                       |
| `LeakDetectionResult`    | Kết quả gồm leak state, severity, reason, timestamp và quality/evidence metadata.                                             |
| `Leak state`             | Trạng thái logic của leak detection: `NORMAL`, `SUSPECTED` hoặc `CONFIRMED`. Evaluation quality được biểu diễn riêng.         |
| `Leak evaluation status` | Khả năng đánh giá leak hiện tại: `NOT_READY`, `ACTIVE`, `DEGRADED` hoặc `UNAVAILABLE`.                                        |
| `Evidence tracker`       | State machine theo dõi một evidence condition qua các phase `INACTIVE`, `PENDING`, `ACTIVE`, `CLEAR_PENDING`, `SUSPENDED`.    |
| `Leak severity`          | Mức độ của leak condition: `NONE`, `LOW`, `MEDIUM`, `HIGH` theo state/reason policy.                                          |
| `Leak reason`            | Primary flow-based reason của state: `NONE`, `CONTINUOUS_FLOW` hoặc `HIGH_FLOW_BURST`; pressure là supporting flag trong MVP. |
| `Evidence window`        | Khoảng thời gian dữ liệu được dùng để đánh giá leak condition.                                                                |
| `Leak event`             | Sự kiện được tạo khi leak state thay đổi hoặc thỏa notification policy.                                                       |

Quy tắc:

* `Leak detection` tạo dấu hiệu/kết luận theo thuật toán; không đồng nghĩa cảm biến trực tiếp nhìn thấy vị trí rò rỉ.
* Không xác nhận leak từ measurement invalid hoặc stale.
* Temperature chủ yếu hỗ trợ compensation và quality; không phải bằng chứng duy nhất của leak.
* Pressure có thể là input bổ sung cho leak detection nhưng không bắt buộc với mọi rule.
* Threshold số học hiện là `TBD/configurable`; state/evaluation/evidence semantics được chốt trong `01_principle/06_leak_detection_state_and_evidence_model.md`.

---

## 8. Runtime Data and Repository Terms

| Thuật ngữ            | Định nghĩa                                                                          | Không nên gọi lẫn                     |
| -------------------- | ----------------------------------------------------------------------------------- | ------------------------------------- |
| `DataRepository`     | Logical service giữ và publish runtime data nhất quán.                              | Global buffer, sensor buffer          |
| `RuntimeSnapshot`    | Bản chụp runtime mới nhất đã publish để LCD, telemetry, storage và diagnostics đọc. | Raw measurement, telemetry payload    |
| `Atomic snapshot`    | Snapshot được thay thế theo cách consumer không đọc dữ liệu cập nhật dở.            | Một biến global không có bảo vệ       |
| `Versioned snapshot` | Snapshot có version/counter để consumer kiểm tra consistency và freshness.          | Firmware version                      |
| `Snapshot version`   | Số thứ tự thay đổi mỗi lần publish snapshot thành công.                             | Schema version, report sequence       |
| `Publish timestamp`  | Thời điểm snapshot được publish.                                                    | Measurement timestamp của từng sensor |
| `Freshness`          | Mức độ mới của dữ liệu so với maximum age requirement.                              | Accuracy                              |
| `Stale data`         | Dữ liệu cũ hơn giới hạn cho phép dù trước đó có thể hợp lệ.                         | Invalid raw data                      |
| `Quality flag`       | Cờ mô tả tính hợp lệ, freshness hoặc trạng thái processing.                         | Error code toàn hệ thống              |
| `Data owner`         | Service duy nhất chịu trách nhiệm thay đổi một data object.                         | Mọi module có pointer tới data        |
| `Consumer`           | Module chỉ đọc hoặc sử dụng dữ liệu qua interface được định nghĩa.                  | Owner                                 |

`RuntimeSnapshot` dự kiến gồm:

```text
Snapshot version
Publish timestamp
FlowResult
VolumeState
TemperatureResult
PressureResult
LeakDetectionResult
PowerStatus
ConnectivityStatus
ReportingStatus
SystemStatus and error flags
```

Flow, pressure và temperature có thể có sample interval khác nhau nên cần timestamp/freshness riêng.

---

## 9. Configuration Terms

| Thuật ngữ                 | Định nghĩa                                                                                                                                                                         | Owner                                 |
| ------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------- |
| `DefaultConfig`           | Cấu hình mặc định được firmware cung cấp khi persistent config không hợp lệ hoặc chưa tồn tại.                                                                                     | Firmware/configuration definition     |
| `PendingConfig`           | Cấu hình ứng viên đã nhận nhưng chưa trở thành cấu hình đang hoạt động.                                                                                                            | `ConfigRepository`                    |
| `ActiveConfig`            | Cấu hình đang được các service sử dụng trong runtime.                                                                                                                              | `ConfigRepository`                    |
| `Config validation`       | Kiểm tra type, range, dependency, permission và system constraint.                                                                                                                 | `ConfigRepository`/policy             |
| `Configuration commit`    | Quá trình lưu configuration record hợp lệ vào persistent storage.                                                                                                                  | `StorageService`                      |
| `Configuration apply`     | Quá trình affected service áp dụng matching `ActiveConfig` version tại safe boundary và trả versioned status; khác với persistent commit và repository active-version replacement. | Affected service + `ConfigRepository` |
| `ConfigApplyRequest`      | Request từ `ConfigRepository` tới affected service, gồm `transaction_id`, `config_version` và changed-field mask.                                                                  | `ConfigRepository`                    |
| `ConfigApplyResult`       | Per-service acknowledgement cho matching config version với một trong các status `APPLIED`, `DEFERRED` hoặc `REJECTED`, kèm reason.                                                | Affected service                      |
| `APPLIED`                 | Service đã áp dụng matching config version thành công tại safe boundary.                                                                                                           | Config apply contract                 |
| `DEFERRED`                | Service đã nhận matching config version nhưng chưa thể apply; phải giữ reason và tiếp tục tại safe boundary được định nghĩa.                                                       | Config apply contract                 |
| `REJECTED`                | Service từ chối matching config version vì incompatibility hoặc runtime constraint; không được báo thành fully applied.                                                            | Config apply contract                 |
| `Configuration rollback`  | Khôi phục config trước đó/default khi apply hoặc persistent validation thất bại.                                                                                                   | `ConfigRepository`/`StorageService`   |
| `CommandDispatcher`       | Logical service nhận command đã validate và chuyển thành application event/action.                                                                                                 | Application layer                     |
| `Configuration authority` | Thành phần có quyền quyết định config có hợp lệ và được apply hay không; trong baseline là MCU, không phải BLE module.                                                             | MCU/config services                   |

Configuration flow chuẩn:

```text
BLE request
  -> BleConfigService
  -> frame and permission validation
  -> PendingConfig
  -> configuration validation
  -> StorageService commit if required
  -> ActiveConfig
  -> versioned ConfigApplyRequest to affected services
  -> APPLIED / DEFERRED / REJECTED results
  -> aggregate BLE response
```

Không dùng các mô tả sau:

```text
BLE writes directly to F-RAM
BLE module owns configuration
UART callback changes ActiveConfig immediately
```

---

## 10. Storage Terms

| Thuật ngữ           | Định nghĩa                                                                                      |
| ------------------- | ----------------------------------------------------------------------------------------------- |
| `Persistent data`   | Dữ liệu cần giữ sau reset hoặc mất nguồn.                                                       |
| `Persistent record` | Cấu trúc dữ liệu có header/version/payload/integrity metadata được lưu vào nonvolatile storage. |
| `StorageService`    | Logical service duy nhất được phép load/commit persistent records.                              |
| `F-RAM`             | Nonvolatile memory có tốc độ ghi cao; baseline dùng FM24CL04B.                                  |
| `A/B slot`          | Cơ chế giữ hai slot record để giảm nguy cơ mất dữ liệu khi commit bị gián đoạn.                 |
| `Active slot`       | Slot chứa record hợp lệ mới nhất theo sequence/version policy.                                  |
| `Inactive slot`     | Slot được ghi và verify trước khi trở thành active.                                             |
| `CRC`               | Mã kiểm tra tính toàn vẹn của persistent record hoặc communication frame.                       |
| `Storage budget`    | Giới hạn dung lượng dành cho từng loại persistent record.                                       |
| `Dirty flag`        | Dấu hiệu cho biết runtime data cần được checkpoint/commit theo policy.                          |
| `Checkpoint`        | Việc lưu trạng thái quan trọng tại một thời điểm hoặc threshold xác định.                       |
| `TelemetryQueue`    | Hàng đợi các `TelemetryRecord` đang chờ gửi hoặc xác nhận. Storage backing hiện là `TBD`.       |
| `Offline retention` | Khoảng thời gian/số lượng telemetry phải được giữ khi 4G không khả dụng.                        |

FM24CL04B không tự động được xem là đủ cho `TelemetryQueue`. Storage capacity phải được đánh giá sau khi chốt payload size và offline retention.

---

## 11. Time and RTC Terms

| Thuật ngữ              | Định nghĩa                                                                                 | Owner                   |
| ---------------------- | ------------------------------------------------------------------------------------------ | ----------------------- |
| `RTC`                  | Real-Time Clock hardware dùng để duy trì date/time và tạo alarm/wakeup.                    | STM32 hardware          |
| `RtcDriver`            | Driver bọc hardware RTC operations; không chứa reporting policy.                           | Driver layer            |
| `TimeService`          | Logical service quản lý timestamp, time validity, timezone và time synchronization.        | Service layer           |
| `System time`          | Thời gian chuẩn mà firmware sử dụng cho timestamp và scheduling.                           | `TimeService`           |
| `Wall-clock time`      | Date/time theo lịch, có liên quan timezone.                                                | `TimeService`           |
| `Monotonic time`       | Counter luôn tăng dùng cho timeout/duration, không bị ảnh hưởng khi wall-clock được chỉnh. | Timebase/platform       |
| `Time validity`        | Trạng thái cho biết system time có đủ tin cậy để timestamp/schedule hay không.             | `TimeService`           |
| `Time source`          | Nguồn dùng để thiết lập/đồng bộ thời gian, ví dụ BLE, 4G network hoặc server.              | `TimeService`           |
| `Time synchronization` | Quá trình cập nhật RTC/system time từ time source hợp lệ.                                  | `TimeService`           |
| `Timezone offset`      | Độ lệch giữa UTC và local time dùng cho reporting window.                                  | `TimeService`/config    |
| `RTC alarm`            | Hardware event tại thời điểm được cấu hình, có thể đánh thức MCU.                          | `RtcDriver`             |
| `Wake reason`          | Nguyên nhân khiến MCU thoát low-power, ví dụ RTC alarm hoặc sensor event.                  | Platform/`PowerManager` |

Quy tắc phân tách:

```text
RtcDriver
  -> đọc/đặt RTC và alarm

TimeService
  -> timestamp, validity, timezone và synchronization

ReportingScheduler
  -> reporting window, interval và next report time
```

---

## 12. Reporting and Telemetry Terms

| Thuật ngữ              | Định nghĩa                                                                                                            |
| ---------------------- | --------------------------------------------------------------------------------------------------------------------- |
| `ReportingScheduler`   | Service xác định khi nào cần tạo báo cáo dựa trên system time và reporting configuration.                             |
| `ReportingSchedule`    | Toàn bộ cấu hình lịch báo cáo đang áp dụng, gồm đúng hai reporting window trong baseline hiện tại.                    |
| `ReportingWindow`      | Khoảng thời gian trong chu kỳ 24 giờ có start time và report interval riêng.                                          |
| `ReportingWindow[0]`   | Reporting window thứ nhất. Start time và interval được cấu hình qua BLE; default/example interval ban đầu là 15 phút. |
| `ReportingWindow[1]`   | Reporting window thứ hai. Start time và interval được cấu hình qua BLE; default/example interval ban đầu là 5 phút.   |
| `Window start time`    | Thời điểm bắt đầu một reporting window, thường biểu diễn bằng số phút kể từ đầu ngày local time.                      |
| `Window end boundary`  | Điểm kết thúc của một reporting window; được suy ra từ start time của window còn lại theo chu kỳ 24 giờ.              |
| `Report interval`      | Khoảng thời gian giữa hai lần đến hạn tạo báo cáo trong một reporting window.                                         |
| `Next report time`     | Thời điểm `ReportingScheduler` dự kiến phát report event tiếp theo.                                                   |
| `REPORT_DUE`           | Event logic cho biết một báo cáo đã đến hạn được tạo; không đồng nghĩa gửi thành công.                                |
| `TelemetryRecord`      | Bản ghi versioned được tạo từ `RuntimeSnapshot` để gửi lên server.                                                    |
| `Telemetry schema`     | Quy định field, type, unit và encoding của dữ liệu server-facing.                                                     |
| `Schema version`       | Version của telemetry data contract. Không phải snapshot version.                                                     |
| `Report sequence`      | Số thứ tự của telemetry record dùng cho trace/deduplication.                                                          |
| `Telemetry generation` | Tạo `TelemetryRecord` từ snapshot hợp lệ.                                                                             |
| `Telemetry queueing`   | Đưa record vào `TelemetryQueue`.                                                                                      |
| `Telemetry delivery`   | Quá trình gửi record qua 4G/server protocol.                                                                          |
| `Delivery result`      | Kết quả delivery: thành công, thất bại, timeout hoặc chưa xác định.                                                   |
| `Acknowledgement`      | Xác nhận từ protocol/server rằng record đã được nhận theo contract. Cơ chế hiện là `TBD`.                             |
| `Retry`                | Thử gửi lại sau delivery failure theo giới hạn/policy.                                                                |
| `Backoff`              | Khoảng trì hoãn tăng hoặc được điều chỉnh giữa các lần retry.                                                         |
| `Offline mode`         | Trạng thái connectivity khi thiết bị chưa thể gửi telemetry nhưng measurement vẫn hoạt động.                          |

Telemetry flow chuẩn:

```text
RTC/time event
  -> TimeService
  -> ReportingScheduler
  -> REPORT_DUE
  -> RuntimeSnapshot
  -> TelemetryRecord
  -> TelemetryQueue
  -> CellularTelemetryService
  -> 4G module
  -> Remote server
```

---

## 13. BLE and Local Configuration Terms

| Thuật ngữ               | Định nghĩa                                                                                 |
| ----------------------- | ------------------------------------------------------------------------------------------ |
| `Bluetooth Low Energy`  | Wireless technology dùng cho local configuration/service trong baseline.                   |
| `BLE`                   | Viết tắt chuẩn của Bluetooth Low Energy.                                                   |
| `BLE client`            | Mobile/PC tool khởi tạo local configuration/service request.                               |
| `BLE module`            | Hardware module thực hiện BLE radio/link và trao đổi với MCU qua UART.                     |
| `BLE UART`              | Dedicated UART giữa MCU và BLE module.                                                     |
| `BleConfigService`      | Firmware service parse/validate request và chuyển config/command vào application boundary. |
| `BLE session`           | Khoảng thời gian BLE client kết nối và thực hiện các request.                              |
| `BLE characteristic`    | Data endpoint trong BLE GATT model; UUID/permission thuộc communication specification.     |
| `Pairing`               | Quá trình thiết lập quan hệ xác thực/khóa giữa BLE devices tùy security policy.            |
| `Bonding`               | Lưu thông tin pairing để tái sử dụng cho lần kết nối sau.                                  |
| `Authorization`         | Kiểm tra client/command có quyền thực hiện operation hay không.                            |
| `Transparent UART mode` | BLE module chuyển byte BLE↔UART gần như trong suốt; chưa chốt cho module cụ thể.           |
| `AT-command mode`       | MCU điều khiển BLE module bằng command/response profile; chưa chốt cho module cụ thể.      |

BLE không phải owner của configuration. MCU vẫn validate và quyết định apply/commit.

---

## 14. 4G and Server Connectivity Terms

| Thuật ngữ                  | Định nghĩa                                                                                                                                                                 |
| -------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `4G module`                | Cellular modem/module giao tiếp với MCU qua UART.                                                                                                                          |
| `Cellular UART`            | Dedicated UART giữa MCU và 4G module.                                                                                                                                      |
| `CellularTelemetryService` | Service điều phối modem connection và telemetry delivery ở mức application/service.                                                                                        |
| `Cellular network`         | Mạng di động vận chuyển data connection của thiết bị.                                                                                                                      |
| `Network registration`     | Quá trình 4G module đăng ký vào mạng di động.                                                                                                                              |
| `Data connection`          | Kết nối packet-data cho application protocol.                                                                                                                              |
| `Modem command`            | Command điều khiển 4G module, thường theo module-specific profile.                                                                                                         |
| `Unsolicited response`     | Thông báo do modem tự phát sinh mà không chờ command trực tiếp.                                                                                                            |
| `Telemetry uplink`         | Luồng device-to-server gửi telemetry; thuộc baseline.                                                                                                                      |
| `Remote downlink`          | Luồng server-to-device command/config; không thuộc baseline theo `DEC-ARCH-008`. Response/ack/time thuộc telemetry/time contract không được coi là generic remote command. |
| `Application protocol`     | MQTT, HTTPS, TCP/custom protocol hoặc lựa chọn khác; hiện là `TBD`.                                                                                                        |
| `Device identity`          | Identifier dùng để server phân biệt và xác thực thiết bị.                                                                                                                  |
| `Endpoint authentication`  | Cơ chế xác nhận server/endpoint hợp lệ.                                                                                                                                    |
| `Credential`               | Key, token, certificate hoặc secret dùng cho authentication.                                                                                                               |
| `Connectivity status`      | Trạng thái modem, registration, connection và delivery được publish cho diagnostics.                                                                                       |

Không gọi `4G module` là `server`. Module chỉ cung cấp connectivity/transport; server là endpoint bên ngoài.

---

## 15. LCD and Display Terms

| Thuật ngữ         | Định nghĩa                                                                  |
| ----------------- | --------------------------------------------------------------------------- |
| `LCD`             | Hardware display của thiết bị. Model/interface hiện là `TBD`.               |
| `LcdService`      | Logical service chọn và định dạng dữ liệu cần hiển thị.                     |
| `lcd_driver`      | Driver giao tiếp trực tiếp với LCD hardware.                                |
| `Display model`   | Bản dữ liệu đã định dạng cho LCD, được tạo từ `RuntimeSnapshot`/status.     |
| `Display refresh` | Hoạt động cập nhật LCD theo chu kỳ hoặc data-change event.                  |
| `Display page`    | Nhóm thông tin được hiển thị cùng nhau nếu LCD hỗ trợ nhiều màn hình/trang. |

LCD là consumer của runtime data, không phải measurement data owner.

---

## 16. Power and Low-Power Terms

| Thuật ngữ                | Định nghĩa                                                                                                                                                            |
| ------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Power subsystem`        | Battery/power input, regulator, monitor và optional power-domain control.                                                                                             |
| `PowerManager`           | Service duy nhất quyết định khi nào hệ thống được vào low-power.                                                                                                      |
| `Power domain`           | Nhóm hardware có thể được cấp/tắt/giám sát độc lập nếu thiết kế hỗ trợ.                                                                                               |
| `Power blocker`          | Điều kiện ngăn MCU vào low-power, ví dụ active measurement, storage commit hoặc 4G transaction.                                                                       |
| `Low-power mode`         | Trạng thái giảm tiêu thụ khi không có công việc quan trọng.                                                                                                           |
| `Idle`                   | Trạng thái runtime không có event đang được xử lý nhưng chưa nhất thiết đã vào hardware low-power.                                                                    |
| `Battery low`            | Điều kiện cảnh báo pin thấp nhưng hệ thống còn có thể vận hành theo policy.                                                                                           |
| `Battery critical`       | Điều kiện nguồn nghiêm trọng; nếu được phát hiện sớm firmware có thể dừng operation không thiết yếu, nhưng không được giả định đủ năng lượng để ghi persistent state. |
| `Voltage sag`            | Sụt áp tạm thời do tải, đặc biệt cần xem xét khi 4G transmit.                                                                                                         |
| `Brownout reset`         | Hardware reset do điện áp xuống dưới ngưỡng bảo vệ. Theo `DEC-PWR-002`, reset đưa firmware về `INIT`; không có controlled shutdown sequence.                          |
| `Reset-safe persistence` | Persistent design vẫn chọn được record hợp lệ sau reset tại bất kỳ commit phase nào, không phụ thuộc emergency flush.                                                 |
| `Controlled shutdown`    | Chuỗi firmware đóng operation trước mất nguồn có bảo đảm năng lượng. Không được hardware hiện tại hỗ trợ và không thuộc baseline.                                     |
| `Wake source`            | Nguồn event có thể đánh thức MCU, ví dụ RTC alarm hoặc MAX35103 INT.                                                                                                  |

---

## 17. Error and Diagnostics Terms

| Thuật ngữ              | Định nghĩa                                                                                                     |
| ---------------------- | -------------------------------------------------------------------------------------------------------------- |
| `Info event`           | Sự kiện không ảnh hưởng hoạt động, có thể chỉ tăng counter hoặc log.                                           |
| `Warning`              | Bất thường chưa làm mất chức năng chính nhưng cần publish/monitor.                                             |
| `Recoverable fault`    | Lỗi có thể xử lý bằng bounded retry, re-init hoặc fallback.                                                    |
| `Critical fault`       | Lỗi khiến hệ thống không thể bảo đảm dữ liệu hoặc vận hành an toàn theo requirement.                           |
| `Fault domain`         | Phạm vi nguồn/ownership của fault, ví dụ `MAX_MEASUREMENT`, `PRESSURE_MEASUREMENT`, `STORAGE` hoặc `CELLULAR`. |
| `Fault severity`       | Mức nghiêm trọng hiện tại: `INFO`, `WARNING`, `ERROR` hoặc `CRITICAL`; không đồng nhất với `SystemMode`.       |
| `Fault impact`         | Loại ảnh hưởng chức năng như data quality, feature unavailable, data integrity hoặc system availability.       |
| `Fault recoverability` | Khả năng xử lý bằng transient retry, local recovery, fallback, system recovery hoặc controlled reinitialize.   |
| `Fault lifecycle`      | Trạng thái của fault: `INACTIVE`, `ACTIVE`, `RECOVERING`, `CLEARED` hoặc `LATCHED`.                            |
| `FaultReport`          | Bản báo cáo fault từ detector chứa identity, domain, context, time và classification hint.                     |
| `DiagnosticRecord`     | Record theo dõi fault lifecycle, counter, recovery attempt và clear/latch metadata.                            |
| `Local recovery`       | Recovery do owner subsystem thực hiện mà không mặc định thay đổi primary `SystemMode`.                         |
| `System recovery`      | Recovery phối hợp nhiều service/shared resource trong `SystemMode.RECOVERY`.                                   |
| `Latched fault`        | Fault hoặc fault history được giữ đến explicit clear/reset/service policy.                                     |
| `Clear condition`      | Bằng chứng cho thấy fault condition đã hết; không chỉ là timeout hết hạn.                                      |
| `Error flag`           | Cờ runtime cho biết một fault đang active hoặc latched theo policy.                                            |
| `Diagnostic counter`   | Bộ đếm sự kiện/lỗi như timeout, invalid sample hoặc failed delivery.                                           |
| `Error code`           | Mã định danh lỗi cụ thể dùng cho diagnostics/telemetry.                                                        |
| `Recovery`             | Quá trình bounded retry, re-init, fallback hoặc subsystem restart.                                             |
| `Degraded mode`        | Hệ thống vẫn cung cấp một phần chức năng nhưng có subsystem hoặc data source không khả dụng.                   |
| `Offline mode`         | Connectivity degraded state khi 4G/server chưa khả dụng; measurement vẫn hoạt động.                            |
| `Fault injection`      | Cơ chế simulation tạo lỗi có kiểm soát để validate firmware behavior.                                          |
| `Missed event`         | Event hardware/application không được xử lý trong điều kiện hoặc deadline yêu cầu.                             |
| `Overrun`              | Event/data mới đến khi lần xử lý trước chưa hoàn tất hoặc buffer không còn chỗ.                                |
| `Timeout`              | Operation không hoàn tất trong thời gian tối đa được định nghĩa.                                               |

Không dùng `error` và `warning` thay thế cho nhau. Severity và recovery policy phải được định nghĩa trong `09_error_handling_overview.md`.

---

## 18. System Mode and Internal State Terms

| Thuật ngữ                  | Mức                           | Định nghĩa                                                                                          | Có nên expose?                                     |
| -------------------------- | ----------------------------- | --------------------------------------------------------------------------------------------------- | -------------------------------------------------- |
| `SystemMode`               | System-level                  | Mode tổng thể của thiết bị.                                                                         | Có thể expose qua BLE/telemetry/LCD ở dạng phù hợp |
| `INIT`                     | System-level                  | Hệ thống đang khởi tạo hardware, config và service.                                                 | Có thể                                             |
| `NORMAL`                   | System-level                  | Measurement, display và scheduling hoạt động bình thường.                                           | Có thể                                             |
| `LOW_POWER`                | System-level                  | Hệ thống giảm năng lượng và chờ wake event.                                                         | Có thể                                             |
| `SERVICE`                  | System-level                  | Chế độ service/factory có kiểm soát.                                                                | Có thể, theo permission                            |
| `ConnectivityStatus`       | System-level status trực giao | Trạng thái tổng hợp của modem/network/server; không phải primary `SystemMode`.                      | Có thể                                             |
| `OFFLINE`                  | Connectivity status           | 4G/server không khả dụng nhưng core measurement còn hoạt động; `SystemMode` có thể vẫn là `NORMAL`. | Có thể                                             |
| `ERROR`                    | System-level                  | Có critical condition cần xử lý hoặc hạn chế operation.                                             | Có thể                                             |
| `RECOVERY`                 | System-level                  | Hệ thống đang thực hiện recovery policy.                                                            | Có thể                                             |
| `MeasurementPhase`         | Firmware internal             | Pha nội bộ của measurement cycle.                                                                   | Không nên expose trực tiếp                         |
| `PressureMeasurementPhase` | Firmware internal             | Pha nội bộ của pressure measurement.                                                                | Không nên expose trực tiếp                         |
| `BleConfigState`           | Firmware internal             | State parser/session của BLE configuration.                                                         | Không nên expose trực tiếp                         |
| `CellularConnectionState`  | Firmware internal             | State modem/network connection.                                                                     | Chỉ expose status tổng hợp                         |
| `TelemetryDeliveryState`   | Firmware internal             | State generate/queue/send/ack của telemetry.                                                        | Chỉ expose status tổng hợp                         |
| `StorageCommitState`       | Firmware internal             | State validate/write/verify/switch của persistent commit.                                           | Không nên expose trực tiếp                         |

Không trộn `SystemMode` và internal phase trong cùng một FSM hoặc data field nếu chưa có mapping rõ ràng.

---

## 19. Interface and Ownership Terms

| Thuật ngữ                | Định nghĩa                                                                                                                                       |
| ------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| `Physical interface`     | Đường kết nối hardware như SPI, I2C, UART, GPIO hoặc analog.                                                                                     |
| `External interface`     | Boundary giữa thiết bị và client/network/server/tool bên ngoài.                                                                                  |
| `Logical interface`      | Contract dữ liệu/hành vi giữa firmware services.                                                                                                 |
| `Interface owner`        | Module duy nhất điều phối một interface hoặc session.                                                                                            |
| `I2cBusManager`          | Logical owner duy nhất của một physical I2C instance; quản lý arbitration, transaction admission, timeout, cancellation và bus recovery.         |
| `Producer`               | Thành phần tạo data/event.                                                                                                                       |
| `Consumer`               | Thành phần nhận hoặc sử dụng data/event.                                                                                                         |
| `Boundary`               | Điểm phân tách trách nhiệm, data ownership hoặc trust.                                                                                           |
| `Driver`                 | Implementation trực tiếp của hardware/protocol boundary thấp.                                                                                    |
| `Service`                | Logical component thực hiện policy hoặc use-case cấp hệ thống.                                                                                   |
| `Repository`             | Component sở hữu và publish một nhóm runtime/config data.                                                                                        |
| `Snapshot double buffer` | Hai buffer `RuntimeSnapshot` trong đó writer chỉ sửa inactive buffer rồi atomic swap active index sau khi hoàn tất publication metadata/barrier. |
| `OTA`                    | Firmware update qua external communication. Không thuộc baseline hiện tại và cần architecture/security review riêng nếu bổ sung.                 |
| `Adapter`                | Component chuyển đổi interface/data model giữa hai boundary.                                                                                     |
| `Callback`               | Hàm được gọi khi driver/HAL báo event; không mặc định là nơi xử lý business logic.                                                               |
| `ISR`                    | Interrupt Service Routine; chỉ nên xử lý tối thiểu và post event.                                                                                |

Interface ID chuẩn:

```text
IF-xx  -> physical hoặc external interface
LIF-xx -> logical service/data interface
```

---

## 20. Canonical Service-to-File Mapping

| Logical name                 | Firmware file/module           | Vai trò chuẩn                                                                                                     |
| ---------------------------- | ------------------------------ | ----------------------------------------------------------------------------------------------------------------- |
| `SystemManager`              | `system_manager`               | Boot, self-check, SystemMode và high-level coordination                                                           |
| `MeasurementManager`         | `measurement_manager`          | Điều phối MAX35103 measurement cycle                                                                              |
| `FlowComputationService`     | `flow_computation_service`     | Tính flow từ validated ultrasonic measurement                                                                     |
| `PressureMeasurementService` | `pressure_measurement_service` | Điều phối pressure sensor sampling                                                                                |
| `PressureProcessingService`  | `pressure_processing_service`  | Validate, filter và calibration pressure                                                                          |
| `CalibrationService`         | `calibration_service`          | Temperature conversion/calibration và owner của `TemperatureResult`; flow calibration và temperature compensation |
| `VolumeAccumulator`          | `volume_accumulator`           | Tích lũy volume từ valid flow result                                                                              |
| `LeakDetectionService`       | `leak_detection_service`       | Phân tích flow/volume/time/pressure và tạo leak result                                                            |
| `DataRepository`             | `data_repository`              | Publish `RuntimeSnapshot` nhất quán                                                                               |
| `ConfigRepository`           | `config_repository`            | Quản lý default/pending/active config                                                                             |
| `StorageService`             | `storage_service`              | Load/commit persistent records                                                                                    |
| `TimeService`                | `time_service`                 | System time, validity, timezone và synchronization                                                                |
| `ReportingScheduler`         | `reporting_scheduler`          | Reporting window, interval và next report time                                                                    |
| `BleConfigService`           | `ble_config_service`           | BLE configuration/session boundary                                                                                |
| `CellularTelemetryService`   | `cellular_telemetry_service`   | 4G modem/session và telemetry delivery                                                                            |
| `LcdService`                 | `lcd_service`                  | Chuyển runtime data thành display model                                                                           |
| `PowerManager`               | `power_manager`                | Power blocker, sleep và wake coordination                                                                         |
| `DiagnosticsService`         | `diagnostics_service`          | Error/status/counter publication                                                                                  |
| `HealthMonitor`              | `health_monitor`               | Watchdog, liveness, stale data và subsystem health                                                                |
| `CommandDispatcher`          | `command_dispatcher`           | Chuyển command hợp lệ thành application action/event                                                              |

---

## 21. Terms to Avoid or Clarify

| Không nên dùng                     | Nên dùng                                                         | Lý do                                                                           |
| ---------------------------------- | ---------------------------------------------------------------- | ------------------------------------------------------------------------------- |
| `MAX sensor`                       | `MAX35103 measurement IC`                                        | MAX35103 không phải pressure sensor và cần phân biệt với transducer.            |
| `Pressure data`                    | `RawPressureMeasurement` hoặc `PressureResult`                   | Phân biệt raw và processed data.                                                |
| `Latest data`                      | `RuntimeSnapshot`                                                | Làm rõ consistency, version và ownership.                                       |
| `Global buffer`                    | `DataRepository`/`RuntimeSnapshot`                               | Tránh mơ hồ data ownership.                                                     |
| `Bluetooth`                        | `BLE` hoặc `BLE module`                                          | Baseline sử dụng Bluetooth Low Energy.                                          |
| `BLE saves config`                 | `BleConfigService creates PendingConfig; StorageService commits` | Làm rõ responsibility.                                                          |
| `4G sends sensor data`             | `CellularTelemetryService sends TelemetryRecord`                 | Sensor không nối trực tiếp với 4G module.                                       |
| `RTC scheduler`                    | `RtcDriver`, `TimeService`, `ReportingScheduler`                 | Ba trách nhiệm khác nhau.                                                       |
| `Morning mode`                     | `ReportingWindow[0]` hoặc `ReportingWindow[1]` theo cấu hình     | Reporting window không phải SystemMode và không mang ý nghĩa buổi sáng cố định. |
| `Night mode`                       | `ReportingWindow[0]` hoặc `ReportingWindow[1]` theo cấu hình     | Reporting window không phải SystemMode và không mang ý nghĩa ban đêm cố định.   |
| `Report sent` tại `REPORT_DUE`     | `Report due`                                                     | Đến hạn không có nghĩa delivery thành công.                                     |
| `Leak sensor`                      | `LeakDetectionService`                                           | Leak được suy ra từ measurement, không có leak sensor riêng trong baseline.     |
| `CommunicationManager` chung chung | `BleConfigService` hoặc `CellularTelemetryService`               | BLE và 4G có role/state khác nhau.                                              |
| `Task`                             | `Service`, `Manager`, `Scheduler`                                | RTOS chưa được chốt.                                                            |

---

## 22. Acronyms

| Acronym | Full name                                   | Ý nghĩa trong dự án                                                |
| ------- | ------------------------------------------- | ------------------------------------------------------------------ |
| `ADC`   | Analog-to-Digital Converter                 | Đọc tín hiệu analog như battery monitor nếu hardware sử dụng       |
| `ADR`   | Architecture Decision Record                | Tài liệu ghi quyết định kiến trúc                                  |
| `BLE`   | Bluetooth Low Energy                        | Kênh local configuration/service                                   |
| `CRC`   | Cyclic Redundancy Check                     | Kiểm tra tính toàn vẹn của frame/record                            |
| `DMA`   | Direct Memory Access                        | Truyền dữ liệu peripheral giảm CPU intervention                    |
| `EXTI`  | External Interrupt/Event Controller         | Nhận external GPIO event như MAX35103 INT                          |
| `F-RAM` | Ferroelectric Random Access Memory          | Nonvolatile storage baseline                                       |
| `GATT`  | Generic Attribute Profile                   | Data/service model của BLE                                         |
| `GPIO`  | General-Purpose Input/Output                | Tín hiệu digital control/status                                    |
| `HAL`   | Hardware Abstraction Layer                  | STM32 hardware abstraction API                                     |
| `HTTP`  | Hypertext Transfer Protocol                 | Một ứng viên server protocol                                       |
| `I2C`   | Inter-Integrated Circuit                    | Bus cho pressure sensor/F-RAM tùy hardware mapping                 |
| `ISR`   | Interrupt Service Routine                   | Hàm xử lý interrupt mức thấp                                       |
| `LCD`   | Liquid Crystal Display                      | Hiển thị cục bộ                                                    |
| `LSE`   | Low-Speed External oscillator               | Nguồn clock ngoài 32.768 kHz thường dùng cho RTC nếu hardware chọn |
| `LTE`   | Long-Term Evolution                         | Nền tảng mạng cellular 4G                                          |
| `MCU`   | Microcontroller Unit                        | STM32L433RCT6 trong baseline                                       |
| `MQTT`  | Message Queuing Telemetry Transport         | Một ứng viên telemetry protocol                                    |
| `NVM`   | Nonvolatile Memory                          | Bộ nhớ giữ dữ liệu sau mất nguồn                                   |
| `RTC`   | Real-Time Clock                             | Timekeeping, alarm và wake source                                  |
| `RTOS`  | Real-Time Operating System                  | Chưa bắt buộc trong baseline                                       |
| `SPI`   | Serial Peripheral Interface                 | Interface MCU↔MAX35103                                             |
| `SWD`   | Serial Wire Debug                           | Debug/flashing interface                                           |
| `TBD`   | To Be Determined                            | Chưa được chốt                                                     |
| `ToF`   | Time-of-Flight                              | Thời gian truyền ultrasonic signal                                 |
| `UART`  | Universal Asynchronous Receiver/Transmitter | Interface riêng cho BLE và 4G modules                              |
| `UTC`   | Coordinated Universal Time                  | Thời gian chuẩn để timestamp nếu architecture chọn                 |
| `WDT`   | Watchdog Timer                              | Cơ chế giám sát firmware liveness                                  |

---

## 23. Open Terminology Questions

| ID            | Câu hỏi                                                                                                                                                                  | Ảnh hưởng                         |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------- |
| `OQ-TERM-001` | Leak state enum chính thức gồm những trạng thái nào?                                                                                                                     | Data model, LCD, BLE và telemetry |
| `OQ-TERM-002` | Runtime/telemetry encoding và fixed-point scale cho flow (`m³/s` logical), volume (`m³` logical), pressure (`Pa` logical) và temperature (`m°C` runtime baseline) là gì? | RuntimeSnapshot và server payload |
| `OQ-TERM-003` | Tên field encode chính thức cho `ReportingWindow[0]` và `ReportingWindow[1]` trong BLE/server data model là gì?                                                          | Config/data model                 |
| `OQ-TERM-004` | Tên command commit configuration chính thức là gì?                                                                                                                       | BLE communication contract        |
| `OQ-TERM-005` | Tên chính thức của server protocol và delivery acknowledgement là gì?                                                                                                    | Communication và telemetry terms  |
| `OQ-TERM-006` | Telemetry queue được lưu ở MCU storage hay 4G module storage?                                                                                                            | Storage/firmware naming           |

---

## 24. Maintenance Checklist

Khi thêm hoặc sửa thuật ngữ:

```text
[ ] Thuật ngữ có thuộc baseline hiện tại không?
[ ] Thuật ngữ là hardware, service, data object, event hay state?
[ ] Logical name có dùng PascalCase không?
[ ] Firmware module/file có dùng lower_snake_case không?
[ ] Event có dùng EVT_UPPER_SNAKE_CASE không?
[ ] Có trùng nghĩa với thuật ngữ hiện có không?
[ ] Data owner đã được xác định chưa?
[ ] Thuật ngữ có làm thay đổi interface ID hoặc requirement không?
[ ] Có cần cập nhật README, system overview hoặc traceability không?
[ ] Nếu còn chưa chốt, đã ghi TBD/Open Question chưa?
```

Khi đổi canonical service name, phải cập nhật tối thiểu:

```text
glossary.md
01_system_overview.md
02_system_block_diagram.md
10_system_interfaces.md
11_firmware_implication.md
12_system_traceability.md
```

---

## 25. Tài liệu liên quan

| Tài liệu                                  | Vai trò                                               |
| ----------------------------------------- | ----------------------------------------------------- |
| `README.md`                               | Baseline, scope và source-of-truth map                |
| `01_system_overview.md`                   | System purpose, subsystem và design rules             |
| `02_system_block_diagram.md`              | Physical/logical block mapping                        |
| `03_operating_principle.md`               | Flow, pressure, leak detection và reporting principle |
| `08_data_flow.md`                         | Data objects và pipeline relationship                 |
| `10_system_interfaces.md`                 | IF/LIF definition, direction và ownership             |
| `11_firmware_implication.md`              | Logical service sang firmware module mapping          |
| `12_system_traceability.md`               | Requirement mapping giữa documentation groups         |
| `13_reporting_and_connectivity_policy.md` | Reporting/time/offline terminology chi tiết           |
| `../02_hardware/`                         | Hardware-specific term, part number và pin label      |
| `../03_firmware/`                         | Implementation type, API, state và event detail       |
| `../04_communication/`                    | BLE/4G/server data model và protocol terms            |
| `../08_simulation/`                       | Emulator, virtual time, fault injection và test terms |

---

## 26. Kết luận

Glossary này là nguồn chuẩn cho cách gọi component, service, data object, event, state và interface trong dự án.

Chuỗi thuật ngữ cốt lõi của hệ thống là:

```text
MAX35103 and pressure sensor
  -> Raw measurements
  -> Validated and processed results
  -> FlowResult / PressureResult / VolumeState
  -> LeakDetectionResult
  -> DataRepository
  -> RuntimeSnapshot
  -> LCD / Storage / Telemetry

BLE client
  -> BleConfigService
  -> PendingConfig
  -> validation and commit
  -> ActiveConfig

RTC
  -> RtcDriver
  -> TimeService
  -> ReportingScheduler
  -> REPORT_DUE
  -> TelemetryRecord
  -> TelemetryQueue
  -> CellularTelemetryService
  -> 4G module
  -> Remote server
```

Tài liệu mới phải dùng đúng các tên chuẩn trên hoặc cập nhật glossary trước khi đưa thuật ngữ mới vào baseline.
