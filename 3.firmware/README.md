# Firmware Core — Smart Water Flow and Pressure Monitor

Firmware cho hệ thống đo lưu lượng và áp suất nước thông minh trên
STM32L433RCT6, với chiến lược **simulation-first trên Linux** trước khi
port sang STM32.

## Yêu cầu

- CMake 3.20+
- GCC hoặc Clang (hỗ trợ C11)
- (Optional) AddressSanitizer, UBSan cho Debug build

## Build

```bash
cd firmware
mkdir -p build && cd build

# Debug build (mặc định)
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)

# Release build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Tắt sanitizer (nếu build trên môi trường không hỗ trợ)
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=OFF
make -j$(nproc)
```

## Chạy Test

```bash
# Từ build directory
ctest --output-on-failure

# Hoặc run từng test riêng:
./tests/test_event_queue
./tests/test_scheduler
./tests/test_data_repository
./tests/test_system_fsm
./tests/test_linux_simulation
```

## Chạy Simulator

```bash
./linux_sim
```

Output hiển thị chuỗi chuyển trạng thái hệ thống:

```
=== Phase 1 Core Framework Simulator ===

[PLATFORM] Linux simulation platform initialized
[After init  ] SystemMode = INIT
[After INIT_COMPLETED] SystemMode = NORMAL
[LP request (blocked)] SystemMode = LOW_POWER
[LP request (granted)] SystemMode = LOW_POWER
[After WAKE  ] SystemMode = NORMAL
[After CRITICAL_ERROR] SystemMode = ERROR
```

## Cấu trúc thư mục

```text
firmware/
├── CMakeLists.txt              # Top-level build
├── cmake/
│   └── warnings.cmake          # Warning + sanitizer flags
├── include/
│   ├── core/                   # Public API headers (portable)
│   │   ├── data_model.h        # Canonical types, event IDs
│   │   ├── app_event_queue.h   # Event envelope + queue
│   │   ├── app_event.h         # Event helpers
│   │   ├── scheduler.h         # Monotonic scheduler
│   │   ├── data_repository.h   # Double-buffer snapshot
│   │   ├── mode_guard.h        # FSM guard context
│   │   ├── system_fsm.h        # 6-mode system FSM
│   │   └── app_event_loop.h    # Cooperative event loop
│   └── platform/               # Platform port interfaces
│       ├── monotonic_clock_port.h
│       ├── system_control_port.h
│       └── platform_runtime.h
├── src/
│   ├── core/                   # Portable core modules
│   │   ├── app_event.c
│   │   ├── app_event_queue.c
│   │   ├── app_event_loop.c
│   │   ├── monotonic_scheduler.c
│   │   ├── system_fsm.c
│   │   ├── mode_guard.c
│   │   └── data_repository.c
│   └── platform/
│       └── linux/              # Linux platform backend
│           ├── virtual_clock.c
│           ├── virtual_clock.h
│           ├── linux_system_control.c
│           └── linux_platform_runtime.c
├── apps/
│   └── linux_sim/
│       └── main.c              # Simulator entry point
└── tests/
    ├── CMakeLists.txt
    ├── test_event_queue.c
    ├── test_scheduler.c
    ├── test_data_repository.c
    ├── test_system_fsm.c
    └── test_linux_simulation.c
```

## Kiến trúc

### Nguyên tắc

- **Cooperative non-blocking**: Mọi I/O asynchronous, không busy-wait,
  không `HAL_Delay`
- **Single-writer ownership**: Mỗi object có đúng một owner; consumer
  chỉ đọc immutable snapshot
- **Layered architecture**: Application → Service → Domain → Infrastructure
  → Driver → Platform
- **Deterministic testing**: Virtual clock cho phép test không cần real
  `sleep()`
- **Simulation-first**: Core logic chạy trên Linux trước, platform backend
  (STM32) thay thế sau

### Layers

```text
linux_sim/main.c
    ↓
AppEventLoop (collect → dispatch → publish → idle check)
    ├── EventQueue (5 delivery classes, priority, overflow policy)
    ├── Scheduler (anchored periodic, one-shot)
    ├── SystemModeManager (6-mode FSM, 53 transitions)
    ├── ModeGuardProvider (guard context từ published evidence)
    └── DataRepository (double-buffer snapshot, C11 atomic swap)
        ↓
Narrow platform ports (monotonic clock, system control, runtime)
    ↓
Linux backend (virtual clock, exit-on-reset, stdin poll)
```

## Các quyết định kiến trúc chính

| Quyết định | Lý do |
|---|---|
| C11 `stdatomic.h` cho snapshot swap | GCC + ARM GCC đều hỗ trợ; không cần platform abstraction riêng |
| Struct layout public (không opaque) | Embedded C: static allocation, testability, không dynamic linking |
| Event generation cập nhật tại dispatch | Tránh stale event do mode transition giữa post và dispatch |
| Guard context từ application layer | FSM là pure function; test fixture truyền guard explicit |
