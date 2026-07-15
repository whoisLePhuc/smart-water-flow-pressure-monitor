# Phase 12 AI Implementation Plan — Reporting, Telemetry Queue and Simulated Connectivity

> Loại tài liệu: implementation runbook dành cho AI coding agent, không phải chương kiến trúc sản phẩm.
>
> Repository: `whoisLePhuc/smart-water-flow-pressure-monitor`
>
> Canonical branch: branch chứa Phase 11 commit `03a8280` hoặc descendant đã được xác nhận.
>
> Prerequisite: Phase 11 leak detection và toàn bộ regression Phase 8–11 đã đạt gate của tài liệu này.

## 1. Nhiệm vụ

Triển khai portable reporting/telemetry pipeline chạy deterministic trên Linux simulator, bắt đầu từ wall-clock state và stable `RuntimeSnapshot`, kết thúc tại một scripted delivery outcome sau common transport boundary.

Phase 12 phải hiện thực:

```text
TimeService
  -> ReportingScheduler
  -> EVT_REPORT_DUE + ReportSlotIdentity
  -> TelemetryBuilder
  -> static RAM TelemetryQueue
  -> CellularTelemetryService
  -> scripted delivery adapter
  -> ACK/reject/timeout/retry result
```

Mục tiêu là chứng minh policy, lifecycle, ordering, idempotence, queue capacity, retry và offline isolation mà không cần modem, server, BLE chip hoặc STM32 thật.

Phase này không triển khai wire protocol production. MQTT QoS 1, HTTP POST, versioned JSON, EC200U AT command, TLS và provisioning chỉ được đặt sau port/adapter và được mô phỏng bằng deterministic scripted outcomes.

## 2. Baseline remote đã quan sát

Tại thời điểm viết plan, `origin/main` có:

```text
03a8280 feat(leak): implement Phase 11 — leak detection service
c5b7e7f feat(volume): implement Phase 10 — volume accumulation and persistent checkpoint
```

Phase 11 commit cho thấy:

- canonical leak data-model migration;
- leak configuration validator;
- evidence trackers;
- leak detection/state service;
- ba test suite leak;
- tổng số được commit message báo cáo là `33/33` tests.

AI không được giả định working branch giống baseline này. Trước khi sửa code, phải fetch, ghi lại remote/branch/HEAD và kiểm tra Phase 12 chưa được người khác triển khai.

README tại baseline vẫn có số suite/status cũ và chưa phản ánh Phase 11. Việc cập nhật README chỉ thực hiện ở workstream cuối sau khi tests xác nhận số liệu thực tế.

## 3. Canonical Phase 12 scope

### 3.1. Trong scope

- Portable wall-clock/`TimeService` state và quality model.
- Linux virtual wall clock độc lập virtual monotonic clock.
- Network/server time candidate validation và RTC-holdover model ở mức portable/simulated.
- Exactly two reporting windows.
- Stable report-slot identity, slot dedup và `SKIP_TO_NEXT`.
- `DEFER_UNTIL_VALID` khi wall clock invalid.
- Atomic schedule apply/recompute theo config version/generation.
- Immutable logical `TelemetryRecord` build từ một stable `RuntimeSnapshot`.
- Scheduled-only report trigger.
- Static RAM FIFO có capacity 64, TTL 24 giờ và drop-oldest policy.
- Exactly one active delivery attempt.
- Scripted connectivity/delivery adapter.
- ACK, server reject, transport failure, timeout và outcome-unknown classification.
- Fixed monotonic retry 30 giây, tối đa ba consecutive retries.
- Offline/reconnect isolation khỏi measurement, volume và leak path.
- Deterministic scenarios, boundary tests, normalized traces và CI regression.

### 3.2. Ngoài scope

- EC200U-CN AT command/URC implementation.
- UART driver/HAL cho modem thật.
- MQTT topic/session implementation.
- HTTP URL/header/parser implementation.
- Exact JSON/CBOR payload schema.
- TLS stack, certificate, secret provisioning hoặc APN security.
- Real server/backend.
- BLE frame/parser/GATT protocol.
- Persistent telemetry queue.
- Ghi queue vào FM24CL04B hoặc vùng reserved F-RAM.
- Immediate leak alert; MVP là scheduled-only.
- OTA, remote configuration hoặc generic 4G downlink.
- Automatic MQTT/HTTP failover.
- STM32 RTC/HAL implementation và modem hardware bring-up.
- Power/data/network qualification.

### 3.3. Hardware requirement

Phase 12 không cần phần cứng thật để đạt functional Definition of Done.

Linux chứng minh được:

- slot calculation và time-jump behavior;
- record/queue semantics;
- offline/retry/ACK lifecycle;
- deterministic ordering và bounded execution;
- isolation khỏi measurement pipeline.

Hardware chỉ cần ở phase sau để chứng minh RTC continuity, alarm/wake, UART/modem timing, current peak, network/operator behavior, TLS và server interoperability.

## 4. Canonical decisions không được thay đổi

| Chủ đề | Quyết định |
|---|---|
| Reporting model | Đúng hai window, half-open `[start, next_start)` |
| Default W0 | `06:00`, interval `15 min` |
| Default W1 | `22:00`, interval `5 min` |
| Interval range | Integer `5..60 min` |
| Minimum window | `30 min` |
| Time basis | Versioned fixed UTC offset; VN baseline `UTC+07:00`; no DST MVP |
| Invalid wall time | `DEFER_UNTIL_VALID` |
| Missed slot | `SKIP_TO_NEXT`; không catch-up |
| Trigger | Scheduled-only; leak change không tạo immediate telemetry |
| Queue | Static RAM FIFO, 64 immutable records |
| Retention | TTL 24 giờ |
| Overflow | Drop oldest eligible non-in-flight record |
| Delivery | Exactly one active attempt |
| Retry | Fixed 30 s monotonic; max 3 consecutive retries |
| Success evidence | MQTT PUBACK hoặc HTTP 2xx ở adapter tương lai |
| Phase 12 adapter | Scripted common delivery outcome; không giả protocol thật |
| Persistence | Queue mất khi reset là accepted MVP limitation |

Không đổi các quyết định này để làm test dễ hơn. Nếu source document/decision registry trên branch mới thay đổi, AI phải dừng, mô tả conflict và xin review contract trước implementation.

## 5. Điều kiện vào Phase 12

AI phải thu thập evidence cho checklist sau:

- [ ] Remote, branch và HEAD đã được ghi lại.
- [ ] Phase 11 commit `03a8280` hoặc descendant tương đương hiện diện.
- [ ] Worktree được kiểm tra; thay đổi của user được bảo toàn.
- [ ] Tất cả `AGENTS.md`/repository instructions đã đọc.
- [ ] Clean build và full CTest hiện tại pass.
- [ ] Determinism replay hiện tại pass.
- [ ] Sanitizer/warnings-as-errors pass theo CI baseline.
- [ ] `LeakDetectionResult` canonical đã có trong `RuntimeSnapshot`.
- [ ] Leak result/state events không duplicate.
- [ ] Phase 10 volume/storage regression pass.
- [ ] Data repository cung cấp stable snapshot read contract.
- [ ] Event catalog chứa time/reporting/cellular/telemetry IDs canonical.
- [ ] Monotonic scheduler hỗ trợ one-shot generation và `SKIP` semantics.
- [ ] Linux virtual time có thể chạy nhiều ngày simulated mà không wall-clock wait.
- [ ] Phase 11 integration gaps, nếu có, đã được ghi và không bị che bởi Phase 12.

### 5.1. Phase 11 closure check

Commit Phase 11 hiện quan sát có unit suites cho config/tracker/state, nhưng AI vẫn phải xác nhận các gate mà Phase 12 phụ thuộc:

- leak result thực sự được publish vào repository hoặc có public integration seam;
- `EVT_LEAK_RESULT_UPDATED` và `EVT_LEAK_STATE_CHANGED` obey idempotence;
- final snapshot chứa leak result nhất quán;
- full simulator path có thể tạo snapshot chứa flow/pressure/volume/leak;
- result/event identity ổn định cho telemetry builder.

Nếu các phần này chưa tồn tại, chỉ được thêm một bounded prerequisite commit riêng trước Phase 12. Không được trộn thay đổi leak algorithm/threshold vào reporting workstream.

## 6. Quy tắc bắt buộc cho AI

### 6.1. Preflight

1. `git fetch --all --prune`.
2. Ghi `git remote -v`, branch, HEAD, log Phase 10–11.
3. Kiểm tra `git status --short` và giữ nguyên unrelated changes.
4. Đọc repository instructions.
5. Chạy baseline build/test/determinism.
6. Inventory time, RTC, schedule, telemetry, config, connectivity và event stubs hiện có.
7. Lập bảng “existing / missing / conflicting / deferred”.
8. Không tạo framework song song nếu module/port hiện tại đã sở hữu contract.

### 6.2. Contract-first

- Policy/system document định nghĩa behavior; test không tự tạo product rule.
- Common logical record tách khỏi MQTT/HTTP/wire encoding.
- `RuntimeSnapshot` không được serialize trực tiếp.
- Monotonic time dùng cho retry, timeout, TTL age và ordering.
- Wall clock chỉ dùng schedule/calendar/external timestamp.
- Time quality của system wall clock không được nhập nhằng với `ResultMetadata.time_quality` của measurement.
- Không hard-code credential, endpoint hoặc protocol string production.

### 6.3. Test-first boundary

- Pure schedule/time/queue tests đi trước hoặc cùng implementation.
- Mọi boundary có before/exact/after.
- Mọi async attempt có success/failure/timeout/late/duplicate/stale-generation.
- Expected golden không được sinh bằng chính production function đang test.
- Scenario không dùng `sleep`, host current time hoặc uncontrolled random.

### 6.4. Static and bounded runtime

- Không heap allocation trong portable runtime.
- Queue và payload có compile-time bounded capacity.
- Parser/protocol không thuộc phase; không thêm unbounded string builder.
- Mỗi event-loop dispatch thực hiện hữu hạn work.
- Không loop theo số slot đã miss hoặc elapsed seconds.

### 6.5. Commit discipline

Tách commit theo responsibility:

1. data/time contracts;
2. reporting schedule;
3. telemetry record/builder;
4. telemetry queue;
5. delivery/retry state machine;
6. simulator integration/scenarios;
7. docs/status.

Không gộp EC200U, BLE, STM32 HAL hoặc F-RAM map vào commit Phase 12.

### 6.6. Stop conditions

AI phải dừng và báo cáo nếu:

- decision registry khác bảng canonical ở section 4;
- không có stable snapshot lifetime contract;
- wall clock và monotonic clock chưa thể tách;
- exact wire schema được yêu cầu nhưng communication document chưa tồn tại;
- credential/provisioning implementation được yêu cầu mà security contract chưa chốt;
- queue record không thể bounded trong static memory;
- có yêu cầu lưu telemetry vào F-RAM fixed map;
- Phase 8–11 regression xuất hiện;
- tests cần host current time/network để pass;
- output Linux bị quảng bá thành hardware/network qualification.

## 7. Thứ tự đọc bắt buộc

AI phải đọc theo thứ tự dưới đây. Không chỉ đọc README hoặc heading.

### 7.1. System policy — đọc trước tiên

| Tài liệu | Mục bắt buộc | Logic cần rút ra |
|---|---|---|
| `1.docs/00_overview/13_reporting_and_connectivity_policy.md` | §§4–5 | Status của decision và canonical values |
| Cùng file | §§6–7 | Architecture ownership, time domains/quality/source priority |
| Cùng file | §§8–11 | Two-window model, slot identity, apply, invalid time, time jump |
| Cùng file | §12 | Scheduled-only trigger; no immediate leak report |
| Cùng file | §§13–14 | Logical record và lifecycle |
| Cùng file | §§15–16 | Connectivity status, outcome và ACK boundary |
| Cùng file | §§17–20 | Queue, retry, retention, overflow, reconnect |
| Cùng file | §§21–23 | BLE boundary, forbidden 4G scope, power/concurrency |
| Cùng file | §§24–30 | Status/counters, requirements, scenarios, TBD và completion |

### 7.2. Overview cross-check

| Tài liệu | Mục nên đọc | Mục đích |
|---|---|---|
| `00_open_questions_and_decisions.md` | `DEC-SCHED-001..004`, `DEC-COM-001..004` | Xác nhận decision không đổi |
| `03_operating_principle.md` | Reporting/time/connectivity | System intent |
| `04_main_operation_flow.md` | Reporting, offline, BLE config flows | End-to-end flow |
| `05_sequence_diagrams.md` | Time sync, report due, delivery, retry | Ordering/correlation |
| `06_system_fsm.md`, `07_operating_modes.md` | Orthogonal offline/status | Không biến cellular state thành SystemMode |
| `08_data_flow.md` | Snapshot, telemetry, config lifecycle | Ownership/lifetime |
| `09_error_handling_overview.md` | Communication/time recovery | Escalation boundary |
| `10_system_interfaces.md` | RTC/BLE/4G interface | Logical ports |
| `11_firmware_implication.md` | Time/reporting/telemetry services | Module mapping |
| `12_system_traceability.md` | Reporting/connectivity requirements | Coverage mapping |

### 7.3. Firmware contracts

| Tài liệu | Mục bắt buộc | Logic cần rút ra |
|---|---|---|
| `00_core/00_runtime_decision.md` | Runtime invariants | Cooperative, non-blocking execution |
| `00_core/01_firmware_architecture.md` | §§5.5–5.6, telemetry flow, source-tree mapping | Owner/layer/module names |
| `00_core/02_event_model_and_scheduler.md` | §§7–12, 17.1 catalog | Envelope, generation, scheduler, retry and events |
| `00_core/03_system_fsm_binding.md` | Offline/time/reporting bindings | Mode isolation |
| `00_core/04_data_model_and_ownership.md` | §§7–11, telemetry/config sections | Stable snapshot, record lifecycle, versioning |
| `10_measurement/17_leak_detection.md` | Event/result and scheduled-only boundary | Leak data in snapshot; no immediate send |
| `10_measurement/18_volume_accumulation.md` | Result/persistence boundary | Cumulative volume semantics |
| `20_data_and_storage/22_persistent_storage.md` | Memory map and exclusions | Do not persist queue in F-RAM |
| `50_platform/50_platform_abstraction.md` | §§7.6, 7.13, 10, 13–15 | Monotonic/RTC port and Linux/STM32 mapping |
| `50_platform/51_linux_platform_backend.md` | Virtual time/actions/trace | Deterministic adapter pattern |
| `90_implementation/92_firmware_test_strategy.md` | `FW-TEST-REQ-010`, `013`, `022`, fixed capacity/parser rules | Required gates |
| `90_implementation/93_linux_simulation_integration.md` | Scenario schema, run controller, normalization | Full-stack test path |

### 7.4. Code đọc sau tài liệu

Đọc ít nhất:

```text
2.firmware/src/infrastructure/event/data_model.h
2.firmware/src/infrastructure/event/app_event*.{h,c}
2.firmware/src/infrastructure/event/scheduler.h
2.firmware/src/infrastructure/time/monotonic_scheduler.c
2.firmware/src/infrastructure/repositories/data_repository.c
2.firmware/src/platform/include/monotonic_clock_port.h
2.firmware/src/platform/linux/virtual_clock.c
2.firmware/src/platform/linux/linux_scheduled_action_queue.c
2.firmware/src/simulation/sim_harness.*
2.firmware/src/simulation/scenario_runner.*
2.firmware/src/simulation/normalized_trace.*
2.firmware/src/services/storage_service.*
2.firmware/src/services/leak_detection.*
2.firmware/tests/CMakeLists.txt
```

Sau đó search:

```bash
rg -n "TimeService|ReportingScheduler|TelemetryRecord|TelemetryQueue|REPORT_DUE|TIME_|CONNECTIVITY|TELEMETRY" 2.firmware 1.docs
```

## 8. Workstream 12A — Baseline audit và contract alignment

### 8.1. Inventory current implementation

Lập bảng:

| Area | Existing | Missing/conflict | Action |
|---|---|---|---|
| Wall clock/time state | | | |
| RTC/wall-clock port | | | |
| Reporting schedule | | | |
| Report slot identity | | | |
| Telemetry record | | | |
| Telemetry queue | | | |
| Connectivity/delivery | | | |
| Config apply | | | |
| Simulator actions/trace | | | |

### 8.2. Freeze logical data types

Chốt trước implementation:

```text
SystemTimeQuality
TimeSource
TimeState
ReportingWindow
ReportingSchedule
ReportSlotIdentity
ReportDueContext
ReportingStatus
TelemetryRecord
TelemetryRecordState
TelemetryQueueResult
ConnectivityStatus
DeliveryStatus
DeliveryOutcome
DeliveryAttemptIdentity
```

Không repurpose `ResultMetadata.TimeQuality` để biểu diễn `RTC_HOLDOVER`/`NETWORK_SYNCED`. Measurement timestamp quality và system wall-clock source quality là hai contract khác nhau.

### 8.3. Align common model

Hiện `ConnectivityStatus` baseline code chỉ có `ONLINE/OFFLINE/UNKNOWN`; policy yêu cầu:

```text
NOT_READY / CONNECTING / ONLINE / OFFLINE / DEGRADED
```

Thực hiện controlled migration, sửa exhaustive switches/tests và không dùng cellular internal `SENDING/RECOVERING` làm orthogonal public status.

Thêm `TimeState`, reporting và delivery summary vào `RuntimeSnapshot` chỉ ở mức bounded consumer status; không nhét queue records, credential hoặc mutable service internals vào snapshot.

### 8.4. Gate to 12B

- Type ownership rõ ràng.
- Không enum conflict với policy.
- Phase 11 snapshot/result integration pass.
- Data layout bounded và không wire/persistent coupling.

### 8.5. Suggested commits

1. `test(phase12): capture reporting and telemetry contract gaps`
2. `refactor(data): align time reporting and connectivity domain types`

## 9. Workstream 12B — Wall-clock port và TimeService

### 9.1. Platform boundary

Thêm/hoàn thiện logical port cho:

```text
read current wall time
apply validated wall time
read RTC continuity/validity evidence
arm/cancel alarm request with generation
receive alarm notification
```

Portable core không gọi `time()`, `gettimeofday()` hoặc STM32 HAL trực tiếp.

### 9.2. Linux virtual wall clock

Linux backend cần hai domain độc lập:

```text
virtual monotonic us -> retry/timeout/TTL/deadline
virtual wall seconds -> schedule/calendar/timestamp
```

Scenario phải có thể:

- set invalid/valid wall time;
- step wall clock forward/backward;
- advance monotonic independently;
- change time source/quality/generation;
- inject network time candidate;
- simulate RTC holdover aging;
- inject stale alarm generation.

### 9.3. TimeService state

Tối thiểu:

```text
wall_time_s
valid
system_time_quality
active_source
time_generation
monotonic_observation_us
last_successful_sync_wall_time_s
last_successful_sync_monotonic_us
sync_age_s
max_time_sync_age_s
fixed_utc_offset_minutes
local_time_basis_version
last_sync_result
```

### 9.4. Candidate validation

Network/server time candidate phải kiểm tra:

- supported format/range;
- source priority;
- freshness/observation identity;
- overflow khi offset/conversion;
- stale/duplicate generation;
- plausible update rule được policy cho phép.

Invalid candidate không phá current valid time.

### 9.5. Holdover policy

- Desired sync cadence: 24 giờ.
- Default `max_time_sync_age`: 604800 s (7 ngày).
- RTC continuity hợp lệ và age dưới max: holdover usable.
- Age đạt/exceed max hoặc continuity mất: wall time invalid.
- Changing max age qua validated config phải re-evaluate immediately.

### 9.6. Events

Use canonical:

```text
EVT_TIME_SYNC_RECEIVED
EVT_TIME_VALIDITY_CHANGED
EVT_TIME_GENERATION_CHANGED
EVT_RTC_ALARM
```

Event mang generation/correlation ổn định; duplicate candidate/alarm không tạo side effect lặp.

### 9.7. Tests

- Boot invalid.
- Apply valid network time.
- RTC holdover below/exact/above max age.
- Invalid candidate while current time valid.
- Clock step forward/backward increments generation.
- Offset version update.
- Wall step does not alter monotonic retry deadline.
- Duplicate/stale sync/alarm.
- Overflow/extreme supported dates.

### 9.8. Gate to 12C

- No host wall-clock dependency.
- Time domains independent.
- Time state immutable/publication safe.
- Required events idempotent.

### 9.9. Suggested commits

1. `feat(time): add portable wall-clock and time-state service`
2. `feat(sim): add deterministic virtual wall-clock controls`
3. `test(time): cover sync holdover invalidity and clock steps`

## 10. Workstream 12C — ReportingSchedule và ReportingScheduler

### 10.1. Schedule config

Mỗi schedule gồm đúng hai window:

```text
schedule_version
schedule_generation
fixed_utc_offset_minutes
local_time_basis_version
window[0].start_minute_of_day
window[0].interval_minutes
window[1].start_minute_of_day
window[1].interval_minutes
```

### 10.2. Validator

Reject nếu:

- không đúng hai window;
- starts trùng nhau hoặc ngoài `00:00..23:59`;
- derived cyclic window dưới 30 phút;
- interval ngoài `5..60` hoặc không integer minute;
- offset/version unsupported;
- arithmetic chuyển đổi có overflow;
- version/generation không hợp lệ.

### 10.3. Window/slot algorithm

Implement pure functions trước service:

```text
select active window from local wall time
derive cyclic window boundaries
compute slot ordinal from window anchor
build stable ReportSlotIdentity
compute first valid due >= current boundary rule
compute next future due
```

Boundary là half-open. Midnight và cross-day window phải test riêng.

### 10.4. Dedup

Stable minimum identity:

```text
schedule_version + window_id + slot_due_wall_time
```

`time_generation` dùng reject stale alarm, không được biến cùng slot thành record mới.

### 10.5. Invalid/missed/time-step behavior

- Invalid time: `DEFERRED_TIME_INVALID`, no `REPORT_DUE`.
- Time recovery: recompute next future slot; no catch-up.
- Forward step/wake late: skip past slots.
- Backward step: do not recreate already accepted identity.
- Alarm generation old: reject.
- Exact due boundary chưa accepted: emit one due.

### 10.6. Atomic schedule apply

```text
validate full candidate
 -> persistent commit/verify through existing config-storage path if in scope
 -> apply at safe scheduler boundary
 -> increment schedule generation
 -> cancel/invalidate old alarm
 -> recompute next due
 -> return APPLIED/DEFERRED/REJECTED
```

Không cần BLE parser; simulator/public config transaction dùng same validated boundary.

### 10.7. Tests

- Defaults produce 64 + 96 = 160 slots/day.
- Exact window boundary.
- Midnight crossing.
- Interval 5/60 and invalid 4/61.
- Minimum 30-minute window.
- Exact due/dedup.
- Forward/backward time step.
- Invalid→valid `SKIP_TO_NEXT`.
- Config apply around due event.
- Stale alarm/schedule generation.
- Multi-day deterministic run.

### 10.8. Gate to 12D

- Pure schedule tests complete.
- No drift from completion time.
- No catch-up burst.
- Exactly one due per stable slot.

### 10.9. Suggested commits

1. `feat(reporting): implement two-window schedule and slot identity`
2. `feat(reporting): add time-aware scheduler and atomic apply`
3. `test(reporting): cover boundaries time jumps and slot dedup`

## 11. Workstream 12D — TelemetryRecord và TelemetryBuilder

### 11.1. Logical record, not wire schema

Define a bounded portable logical record. Do not include JSON/MQTT/HTTP fields that are not canonical.

Minimum groups:

```text
identity:
  device_id/reference
  record_sequence
  schema_version
  report_reason = SCHEDULED

schedule:
  schedule_version
  window_id
  slot_due_wall_time
  slot_ordinal
  report_due_identity/hash or fields

snapshot/time:
  source_snapshot_version
  source_snapshot_generation
  snapshot_build_monotonic_us
  record_build_monotonic_us
  wall_time/time quality/source/generation

product:
  flow + status/provenance summary
  temperature + status/provenance summary
  pressure + status/provenance summary
  volume
  leak state/evidence summary
  system mode
  bounded health/config/calibration versions
```

### 11.2. Builder rules

- Acquire one stable snapshot handle.
- Copy/normalize explicit fields.
- Release handle after build.
- Never serialize runtime struct memory/layout.
- Invalid/unavailable field keeps explicit status; never becomes valid zero.
- Service/calibration/replayed/non-production result does not masquerade as normal production data.
- No credential, key, token, APN secret or unrestricted diagnostic memory.
- After enqueue, record immutable.

### 11.3. Identity and retry

- `record_sequence` increments once per successfully built/admitted logical record according to owner policy.
- Retry keeps same sequence, slot, captured timestamps, volume and payload.
- Duplicate `REPORT_DUE` for same slot cannot build a second record.
- Build failure is explicit and does not claim queued/sent.

### 11.4. Tests

- Stable snapshot consistency under repository update.
- All product fields valid.
- Invalid/stale/unavailable field representation.
- Leak suspected/confirmed summary.
- Volume cumulative value preserved.
- Duplicate due suppression.
- Build failure/capacity boundary.
- No secret fields.
- Record immutable after queue admission.
- Same input/config produces same logical record aside from explicit sequence owner.

### 11.5. Gate to 12E

- Logical record bounded.
- No wire/persistent ABI coupling.
- Snapshot lifetime correct.
- Duplicate slot cannot create duplicate record.

### 11.6. Suggested commits

1. `feat(telemetry): define bounded logical record contract`
2. `feat(telemetry): build immutable record from stable snapshot`
3. `test(telemetry): cover metadata quality and duplicate slots`

## 12. Workstream 12E — Static RAM TelemetryQueue

### 12.1. Ownership and capacity

`TelemetryQueue` is single writer/owner after enqueue.

```text
capacity = 64
backing = static RAM
ordering = oldest eligible first
in-flight = at most one
reset = queue cleared
```

### 12.2. Queue API outcomes

Explicit results:

```text
ENQUEUED
REJECTED_DUPLICATE
DROPPED_OLDEST_AND_ENQUEUED
REJECTED_NO_ELIGIBLE_DROP
REJECTED_INVALID
EXPIRED_REMOVED
ACK_REMOVED
```

Exact names may differ; behavior must remain explicit.

### 12.3. TTL and age

- Maximum age 24 hours.
- Age/order/retry eligibility use monotonic observations, not wall-clock adjustment.
- Expiry has reason/counter.
- Current in-flight record not dropped/mutated by ordinary TTL/overflow handling.

### 12.4. Overflow

When full:

1. expire eligible old records;
2. if still full, drop oldest non-in-flight record;
3. enqueue new record;
4. increment loss counter and retain bounded drop metadata.

If every candidate is protected/in-flight, return explicit failure; never overwrite silently.

### 12.5. Lifecycle

Queue/attempt states cover:

```text
BUILT -> QUEUED -> IN_FLIGHT
IN_FLIGHT -> ACKNOWLEDGED -> REMOVED
IN_FLIGHT -> OUTCOME_UNKNOWN -> QUEUED/retry
QUEUED/OUTCOME_UNKNOWN -> DROPPED
```

State may be split between queue and delivery owner, but there must be one authoritative location for each mutable field.

### 12.6. Tests

- Capacity 0/1/63/64/65 attempts.
- FIFO order.
- Duplicate identity.
- TTL before/exact/after 24h.
- Drop oldest excluding in-flight.
- ACK removes exact record.
- Wrong/late ACK does not remove head.
- Reset clears queue.
- Retry does not mutate payload.
- Counter saturation/wrap policy.

### 12.7. Gate to 12F

- Fixed memory verified.
- Full/overflow/expiry deterministic.
- No F-RAM/storage call.
- Exactly-one in-flight invariant proven.

### 12.8. Suggested commits

1. `feat(telemetry): add static fifo queue with ttl and overflow policy`
2. `test(telemetry): cover capacity expiry drop and immutable retry`

## 13. Workstream 12F — Connectivity and delivery state machine

### 13.1. Public and internal states

Public connectivity:

```text
NOT_READY / CONNECTING / ONLINE / OFFLINE / DEGRADED
```

Internal delivery/cellular state may include:

```text
IDLE / CONNECTING / READY / SENDING / WAIT_RESPONSE /
RETRY_WAIT / RECOVERING / OFFLINE
```

Internal state không trở thành primary `SystemMode`.

### 13.2. Common delivery port

Phase 12 port nhận logical immutable record/reference và trả async terminal/ambiguous outcome:

```text
ACKNOWLEDGED
REJECTED_BY_SERVER
TRANSPORT_FAILED
TIMEOUT
OUTCOME_UNKNOWN
CANCELLED
```

Không encode MQTT/HTTP thật. Scripted adapter phải cho scenario chỉ định outcome, delay, attempt ID, connection generation, duplicate và late completion.

### 13.3. Attempt identity

Completion match tối thiểu:

```text
record_sequence
attempt_sequence/correlation_id
connection_generation
```

Recovery increments generation. Completion thuộc generation/attempt cũ bị reject và tăng stale counter.

### 13.4. ACK/rejection behavior

- `ACKNOWLEDGED`: remove exact record.
- Retryable failure: schedule monotonic retry.
- `OUTCOME_UNKNOWN`: preserve record/payload; retry according to policy.
- Schema/auth/non-retryable reject: do not tight retry; explicit diagnostic/disposition.
- ACK from future MQTT/HTTP adapter only valid after approved protocol mapping.

### 13.5. Retry policy

```text
retry_delay = 30 s monotonic
max_consecutive_retry = 3
```

Sau ba resend attempt liên tiếp:

- giữ record trong queue;
- dừng immediate consecutive retry;
- thử lại ở future connectivity/reporting opportunity theo policy;
- không drop/ack giả;
- measurement event loop tiếp tục bình thường.

### 13.6. Offline/reconnect

- Offline không xóa queue.
- New scheduled record vẫn enqueue nếu capacity cho phép.
- Reconnect deadline độc lập report due.
- Không tight loop.
- Mỗi dispatch thực hiện một bounded service step.
- Measurement events có priority cao hơn modem work.

### 13.7. Tests

- Not-ready/connect/online/offline/recover transitions.
- ACK success.
- Retryable failure at 29,999,999/exact 30,000,000/after.
- Three retry limit.
- Timeout/outcome unknown.
- Late matching ACK before retry disposition.
- Stale ACK after connection recovery.
- Server non-retryable reject.
- Queue receives while offline.
- Measurement proceeds through repeated offline/retry events.
- No duplicate active attempt.

### 13.8. Gate to 12G

- Scripted adapter is deterministic.
- No real network dependency.
- Exactly-one attempt and retry limit proven.
- Offline isolation proven.

### 13.9. Suggested commits

1. `feat(connectivity): add portable delivery state machine and port`
2. `feat(sim): add scripted connectivity and delivery outcomes`
3. `test(connectivity): cover ack retry timeout stale generation and offline`

## 14. Workstream 12G — Application integration và simulated config boundary

### 14.1. Event routing

Bind canonical events:

```text
EVT_RTC_ALARM
EVT_TIME_SYNC_RECEIVED
EVT_TIME_VALIDITY_CHANGED
EVT_TIME_GENERATION_CHANGED
EVT_REPORT_DUE
EVT_CONNECTIVITY_CHANGED
EVT_CELLULAR_STEP_DUE
EVT_TELEMETRY_RECORD_ENQUEUED
EVT_TELEMETRY_RETRY_DUE
EVT_TELEMETRY_DELIVERY_CONFIRMED
EVT_TELEMETRY_DELIVERY_FAILED
```

Mỗi handler phải validate correlation/generation/mode và chạy bounded step.

### 14.2. End-to-end scheduled flow

```text
virtual wall time reaches valid slot
 -> scheduler verifies current time/config/alarm generation
 -> emit one REPORT_DUE
 -> builder captures stable RuntimeSnapshot
 -> queue owns immutable record
 -> connectivity selects eligible head
 -> scripted adapter completes/timeout
 -> queue removes/retries/preserves record
 -> statuses/counters/snapshot update deterministically
```

### 14.3. Config path

Phase 12 không cần BLE parser. Cần public/simulated candidate path chứng minh:

```text
candidate -> validation -> persistent commit/verify if using existing config store
 -> active config replacement -> per-service apply result
```

Nếu full `ConfigRepository` chưa có, không tạo fake “committed” result. Implement minimum transaction boundary rõ ràng hoặc giữ config apply test-only/in-memory và ghi limitation.

### 14.4. Status publication

Runtime snapshot có thể chứa bounded summaries:

```text
TimeState/version
ReportingStatus/next due/schedule version
ConnectivityStatus
DeliveryStatus
queue depth/drop summary
```

Không chứa queue content, secret hoặc mutable pointer.

### 14.5. Power/mode behavior

- Active delivery blocks low-power until safe boundary.
- Offline/retry wait không tự block sleep khi wake deadline arm được.
- SERVICE/ERROR mode behavior theo FSM binding.
- Cellular error cô lập không tự đổi primary mode sang ERROR.
- Measurement and leak continue while offline.

### 14.6. Integration tests

- Schedule due builds/queues exactly one record.
- Snapshot changes after due do not mutate queued record.
- Leak change alone creates no immediate record.
- Time invalid creates no record.
- Recovery skips old slots.
- Offline accumulation then reconnect delivery.
- Queue overflow during offline.
- Schedule update races due event via generation.
- Time sync races retry deadline; retry remains monotonic.
- MAX/pressure event and cellular completion same timestamp: measurement priority first.
- Reboot clears telemetry queue but preserves Phase 10 persistent data.

### 14.7. Gate to 12H

- Full path runs through public boundaries.
- No hidden protocol/hardware dependency.
- One record/slot and one attempt/record invariants pass.
- Phase 8–11 regressions pass.

### 14.8. Suggested commits

1. `feat(app): integrate reporting telemetry and connectivity events`
2. `feat(sim): add end-to-end reporting and offline scenarios`
3. `test(phase12): cover schedule-to-ack and concurrency invariants`

## 15. Workstream 12H — Verification, CI, resource evidence và status

### 15.1. Required test groups

Add at least:

```text
test_time_service
test_reporting_schedule
test_reporting_scheduler
test_telemetry_builder
test_telemetry_queue
test_delivery_service
test_reporting_e2e
test_reporting_determinism
```

Names may match repository convention, but responsibility coverage cannot be merged into one opaque test executable.

### 15.2. Determinism gate

Run every Phase 12 scenario repeatedly with same config/seed and compare normalized trace byte-for-byte. Normalize away pointers, host paths and unstable timestamps; do not normalize semantic record/slot/attempt IDs.

### 15.3. Analysis gate

- Warnings as errors.
- ASan/UBSan on Linux where supported.
- Static analysis for bounds, overflow, enum switch and lifetime.
- No heap symbols in portable Phase 12 runtime if project policy forbids them.
- No real network or current wall-clock use in deterministic target.

### 15.4. Resource evidence

Record:

- `sizeof` TimeState/schedule/record/queue/service contexts;
- total 64-record queue RAM;
- stack estimate for build/evaluate step;
- maximum scheduled actions/events in scenarios;
- bounded step count for long offline/retry run;
- daily default slot count and data/power qualification note.

Do not claim STM32 RAM/power acceptance until target review.

### 15.5. README/status

Sau khi gates pass:

- add Phase 11 leak completion if README still stale;
- add Phase 12 reporting/time/queue/simulated-connectivity completion;
- record actual test-suite count from CTest, không copy commit message;
- keep detailed BLE/4G contracts and real modem integration pending;
- keep STM32 hardware bring-up pending;
- distinguish Linux functional completion from hardware/server/security qualification.

### 15.6. Suggested commits

1. `test(phase12): add deterministic reporting connectivity regression gate`
2. `chore(ci): run phase12 analysis and replay suites`
3. `docs: update roadmap after phase12 simulation completion`

## 16. Test matrix tối thiểu

| ID | Scenario | Expected invariant |
|---|---|---|
| `P12-TIME-001` | Boot wall time invalid | No scheduled report |
| `P12-TIME-002` | Valid server sync | Generation changes once |
| `P12-TIME-003` | Holdover reaches 7-day boundary | Exact validity transition |
| `P12-TIME-004` | Wall step forward/back | Monotonic retries unchanged |
| `P12-SCH-001` | Default day | Exactly 160 distinct slots |
| `P12-SCH-002` | 06:00/22:00 boundaries | Half-open window ownership |
| `P12-SCH-003` | Invalid→valid | Skip old slots, no catch-up |
| `P12-SCH-004` | Duplicate/stale alarm | No duplicate due/record |
| `P12-SCH-005` | Config apply near due | One version owns slot |
| `P12-REC-001` | Stable snapshot build | Coherent record fields |
| `P12-REC-002` | Invalid measurement | Explicit status, no fake zero |
| `P12-REC-003` | Leak changes | No immediate telemetry |
| `P12-QUE-001` | 64→65 records | Drop oldest eligible, counter |
| `P12-QUE-002` | TTL exact 24 h | Exact expiry boundary |
| `P12-QUE-003` | Record in-flight on overflow | In-flight not dropped/mutated |
| `P12-DEL-001` | ACK | Exact head removed |
| `P12-DEL-002` | Timeout | Outcome unknown/retry policy |
| `P12-DEL-003` | Retry 30 s × 3 | No fourth consecutive retry |
| `P12-DEL-004` | Late stale completion | Rejected by generation |
| `P12-OFF-001` | Long offline | Measurement/leak continue |
| `P12-OFF-002` | Reconnect | Oldest eligible first |
| `P12-RST-001` | Reboot | Queue lost; persistent volume intact |
| `P12-DET-001` | Repeat full scenario | Byte-identical normalized trace |

## 17. Verification commands

AI phải dùng repository commands thực tế. Baseline example:

```bash
git status --short
git log --oneline --decorate -15

cmake -S 2.firmware -B 2.firmware/build -DCMAKE_BUILD_TYPE=Debug
cmake --build 2.firmware/build --parallel
ctest --test-dir 2.firmware/build --output-on-failure

cmake -S 2.firmware -B 2.firmware/build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_SANITIZERS=ON
cmake --build 2.firmware/build-asan --parallel
ctest --test-dir 2.firmware/build-asan --output-on-failure
```

Nếu option sanitizer khác hoặc chưa tồn tại, inspect CMake/CI rồi dùng command canonical. Không báo pass cho command chưa chạy.

Additional checks:

```bash
rg -n "time\(|gettimeofday|clock_gettime|sleep\(|usleep\(" 2.firmware/src
rg -n "malloc|calloc|realloc|free" 2.firmware/src
rg -n "credential|password|token|secret|APN" 2.firmware/src
rg -n "FM24|F-RAM|StorageService" 2.firmware/src/services/*telemetry* 2.firmware/src/services/*report* 2.firmware/src/services/*connect* 2>/dev/null
```

Mọi match phải được phân loại; search match không tự động là lỗi.

## 18. Definition of Done

Phase 12 hoàn tất khi tất cả đúng:

- [ ] Phase 11 closure prerequisites pass.
- [ ] Wall clock và monotonic time tách độc lập.
- [ ] TimeService xử lý sync, holdover, invalidity và generation deterministic.
- [ ] Exactly two-window validator/slot algorithm pass all boundaries.
- [ ] Invalid time dùng `DEFER_UNTIL_VALID`.
- [ ] Missed slot dùng `SKIP_TO_NEXT`; no catch-up.
- [ ] Default schedule tạo đúng 160 unique slots/day.
- [ ] TelemetryBuilder tạo immutable bounded record từ one stable snapshot.
- [ ] Invalid data không trở thành valid zero.
- [ ] Leak-state change không tạo immediate production telemetry.
- [ ] Static RAM queue có exactly 64 slots, TTL 24h, drop-oldest policy.
- [ ] Queue không gọi persistent storage/F-RAM.
- [ ] Exactly one active delivery attempt.
- [ ] ACK/reject/failure/timeout/unknown outcomes tách rõ.
- [ ] Retry dùng monotonic 30 s và dừng sau 3 consecutive retries.
- [ ] Stale/duplicate completion không tạo side effect.
- [ ] Offline không dừng measurement/volume/leak/snapshot.
- [ ] Full deterministic schedule-to-delivery scenario pass.
- [ ] Full Phase 8–11 regression pass.
- [ ] Warnings, sanitizer, determinism và bounded-resource gates pass.
- [ ] README ghi đúng Phase 11–12 và actual test count.
- [ ] Documentation không claim real modem/server/STM32/hardware qualification.

## 19. Những việc AI không được làm

- Không implement EC200U AT commands từ datasheet trong Phase 12.
- Không chọn MQTT hay HTTP như transport production nếu chưa có detailed contract.
- Không tự tạo JSON schema production.
- Không gọi internet/server thật trong test.
- Không dùng host current time làm deterministic oracle.
- Không dùng wall clock cho retry/TTL/timeout.
- Không persist telemetry queue vào F-RAM.
- Không đưa secret vào snapshot/record/log.
- Không tạo immediate leak telemetry.
- Không catch-up mọi missed slot.
- Không chạy hai active attempts cho cùng record.
- Không remove record khi outcome chưa được ACK contract xác nhận.
- Không đổi measurement/leak logic để làm telemetry scenario pass.
- Không thêm HAL/POSIX dependency vào portable core.
- Không cập nhật golden hàng loạt mà không giải thích contract change.

## 20. Deliverables bắt buộc

AI phải bàn giao:

1. Source/header cho time/reporting/record/queue/delivery modules.
2. Linux virtual wall-clock và scripted delivery adapter.
3. Common data-model/event/status alignment.
4. Unit tests cho mọi pure module.
5. Integration/e2e deterministic scenarios.
6. Normalized trace/golden evidence.
7. CMake/CI updates.
8. Requirement-test mapping cho `REQ-RCP-*` liên quan.
9. README/status update.
10. Final implementation report.

Final report phải ghi:

```text
remote / branch / HEAD
Phase 11 baseline verified
files changed
workstreams completed
tests added and actual total
commands executed and results
determinism repetitions
sanitizer/static-analysis result
queue/RAM/resource sizes
known limitations
deferred hardware/protocol/security work
next recommended phase
```

## 21. Phase tiếp theo sau Phase 12

Phase 12 tạo portable behavior và simulator evidence. Phase tiếp theo nên tách thành hai milestone, không trộn:

### Phase 13A — Detailed communication contracts and protocol adapters

- BLE request/response schema cho config/service.
- Versioned telemetry JSON schema.
- MQTT QoS 1 hoặc HTTP POST adapter contract cụ thể.
- EC200U AT/URC parser và state machine trên scripted UART.
- Credential/provisioning/security boundary.
- Parser fuzz/malformed/overlength tests.

Phase 13A vẫn có thể bắt đầu trên Linux nếu dùng UART/modem peer emulator.

### Phase 13B — STM32 platform port and hardware bring-up

- STM32 HAL adapters cho clock/RTC alarm/SPI/I2C/GPIO/UART/power.
- On-target platform contract tests.
- MAX/ZSSC/F-RAM bring-up.
- EC200U/nRF52810 UART integration.
- RTC holdover/alarm/STOP2 checks.
- HIL equivalence với normalized Linux evidence.

Chỉ sau các milestone này mới bắt đầu product calibration, network/power characterization, security provisioning và field qualification.