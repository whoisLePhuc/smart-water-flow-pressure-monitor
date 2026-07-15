# P0 Fix Plan — Generation, Dispatch, Data Model

## P0A — Hoàn thiện Data Model

### Tasks

| # | Task | File | Mô tả |
|---|---|---|---|
| A1 | Thêm `MeasurementBindingReference` | `data_model.h` | Struct chứa `binding_id`, `binding_version`, `profile_version` dùng để tracing calibration/config/profile gốc cho mỗi result |
| A2 | Thêm `binding` field vào `ResultMetadata` | `data_model.h` | `MeasurementBindingReference binding` — cho phép consumer kiểm tra version của profile/config gốc khi result được tạo |
| A3 | Đổi tên enum values | `data_model.h` | `MEASUREMENT_PURPOSE_*` → `MEAS_PURPOSE_*`, `DATA_ORIGIN_*` → `DATA_ORIGIN_*` (giữ), kiểm tra consistency với doc |
| A4 | Thêm `data_is_production()` guard | `data_model.h` hoặc `data_repository.h` | `bool data_is_production(const ResultMetadata *meta)` — kiểm tra `purpose==PRODUCTION && origin==LIVE_DEVICE && provenance==MEASURED` |
| A5 | Thêm payload types cho events | `data_model.h` | `MaxSpiCompletionPayload`, `MaxRawReadyPayload`, `I2cTransactionCompletionPayload`, `PressureRawReadyPayload` — structs bounded cho event envelope |

### Kiểm tra
- `data_is_production()` được unit test với mọi tổ hợp hợp lệ/không hợp lệ
- Binding reference được copy/preserve qua repository publication

---

## P0B — Generation Handling

### Vấn đề
- `app_event_loop.c` ghi đè `source_generation` (line 69) — mất generation gốc
- `system_fsm.c` so mọi event với `mode_generation` — trộn domain
- `generation == 0` semantics không nhất quán

### Giải pháp

#### B1. Xoá dòng ghi đè generation trong event loop
**File**: `app_event_loop.c`
- Xoá `events[i].source_generation = system_fsm_get_context(loop->fsm).mode_generation;`
- Event envelope phải giữ nguyên từ producer tới consumer

#### B2. Tách generation domain trong FSM
**File**: `system_fsm.c`
- FSM chỉ kiểm tra `source_generation` với event có `source_id` thuộc system domain (`EVT_SYSTEM_START…EVT_CONTROLLED_REINITIALIZE`)
- Với event ngoài system domain, FSM bỏ qua generation check (generation do owner khác quản lý)
- Cách làm: thêm `event_is_system_event(id)` helper, chỉ check generation nếu true

#### B3. Thống nhất `generation == 0` semantics
- `generation == 0` = **not set / unknown** — bỏ qua mọi generation check
- `generation != 0` = phải match với expected generation của domain tương ứng
- Cập nhật stale check: chỉ reject khi `gen != 0 && gen != expected`

### Kiểm tra
- Unit test: event với generation từ domain khác đi qua FSM không bị stale
- Unit test: generation 0 không bị reject ở bất kỳ domain nào
- Unit test: completion cũ từ scheduler generation bị reject bởi scheduler, không phải FSM

---

## P0C — Event Dispatch

### Vấn đề
- Dequeue batch 8 event → dispatch tối đa 4 → mất event
- Không có event router — mọi event đổ vào FSM
- FSM pending_actions không được thực thi
- Guard context hard-code true
- Scheduler chưa integrate

### Giải pháp

#### C1. Dispatch từng event theo budget
**File**: `app_event_loop.c`
- Thay vì dequeue batch rồi dispatch subset, chuyển sang:
  ```
  while (budget_left > 0 && event_available):
      dequeue một event
      route event đến owner
      budget_left--
  ```
- Không dequeue quá số sẽ dispatch

#### C2. Thêm event router
**File**: mới `app_event_router.h/.c`
- Router phân loại event theo `event_id` range:
  - System events (0x0100-0x01FF) → `SystemModeManager`
  - Measurement events (0x0200-0x02FF) → `MeasurementManager` / service owner
  - Infrastructure/bus events (0x0380-0x03FF) → `I2cBusManager`
  - Config/storage events (0x0400-0x04FF) → `ConfigRepository` / `StorageService`
  - Time/reporting events (0x0500-0x05FF) → `TimeService` / `ReportingScheduler`
  - BLE/cellular events (0x0600-0x06FF) → `BleConfigService` / `CellularTelemetryService`
  - Display/health events (0x0700-0x07FF) → `LcdService` / `HealthMonitor`
- Default (unknown event) → diagnostic counter

#### C3. Execute FSM pending actions
**File**: `app_event_loop.c`, trong turn:
- Sau dispatch, đọc `system_fsm_get_pending_actions()`
- Execute action tokens:
  - `ACTION_START_NORMAL` → enable production scheduler
  - `ACTION_ENTER_ERROR` → quiesce services, set diagnostic
  - `ACTION_PREPARE_LOW_POWER` → signal PowerManager
  - `ACTION_RESUME_NORMAL` → resume production
  - `ACTION_REQUEST_RESET` → platform reset
  - ...
- Clear actions: `system_fsm_clear_actions()`

#### C4. Guard context từ guard provider
**File**: `app_event_loop.c`
- Thay hard-code true bằng `mode_guard_capture()` từ `ModeGuardProvider`
- Guard provider đọc evidence từ `DataRepository` (mode context, readiness flags)
- Khi chưa có owner cho các evidence, dùng default safe values (không block progress)

#### C5. Nối scheduler vào event loop
**File**: `app_event_loop.c`, đầu mỗi turn:
```
now = monotonic_now_us()
scheduler_dispatch_due(now, due_events, max_due)
for each due_event:
    app_event_queue_post(queue, &due_event)
```
- Scheduler phát due event vào queue, không dispatch trực tiếp
- Event loop xử lý due event như mọi event khác qua router

### Kiểm tra
- Budget test: dequeue tối đa budget, không mất event
- Router test: mỗi event range đến đúng owner function
- Action execution test: mode transition → action token được execute
- Guard integration test: mode_guard_capture trả context khác hard-code
- Scheduler integration test: scheduler → queue → router → owner

---

## P0D — Queue & Repository (củng cố)

### D1. Reserved capacity → true reservation
**File**: `app_event_queue.c`
- Thay đổi logic: critical event luôn có thể post đến `reserved_critical` slot, nhưng background event không được lấp đầy capacity đến mức không còn slot cho critical
- Cách làm: background event chỉ được dùng `capacity - reserved_critical` slots

### D2. Fairness / starvation bound
**File**: `app_event_queue.c`
- Thêm per-priority quota trong mỗi turn dequeue
- Low priority không bị starve khi high priority luôn có event

### D3. SourceEventToken kiểm tra
**File**: `data_repository.c`
- Đảm bảo `SourceEventToken.snapshot_published_in_turn` hoạt động đúng với router-based dispatch (không chỉ còn 1 FSM dispatch)

---

## Thứ tự thực hiện

```text
Phase A (Data model)       ─→ Phase B (Generation) ─→ Phase C (Dispatch) ─→ Phase D (Queue)
     │                            │                         │
     ├ A1, A2, A3, A4            ├ B1 (xoá ghi đè)          ├ C1 (dispatch từng event)
     └ A5 (payload types)        ├ B2 (FSM generation)      ├ C2 (event router)
                                  └ B3 (gen 0 semantics)     ├ C3 (FSM actions)
                                                              ├ C4 (guard provider)
                                                              └ C5 (scheduler integration)
```

### Phụ thuộc
- B1 có thể làm song song A (không dependency)
- B2 phụ thuộc B1 (FSM cần event không bị ghi đè)
- C1 phụ thuộc B1, B2 (dispatch đúng generation)
- C2 phụ thuộc A5 (payload types cho router)
- C3 phụ thuộc C1 (dispatch loop mới)
- D1-D3 có thể làm song song hoặc sau C

### Files bị ảnh hưởng

| File | A | B | C | D |
|---|---|---|---|---|
| `data_model.h` | ✅ A1-A5 | | | |
| `app_event_loop.c` | | ✅ B1 | ✅ C1, C3, C4, C5 | |
| `system_fsm.c` | | ✅ B2, B3 | | |
| `data_repository.h` | ✅ A4 | | | |
| `data_repository.c` | ✅ A4 | | | ✅ D3 |
| `app_event_router.h/.c` (new) | | | ✅ C2 | |
| `app_event_queue.c` | | | | ✅ D1, D2 |
| `tests/*` | ✅ | ✅ | ✅ | ✅ |
