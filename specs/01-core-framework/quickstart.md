# Quickstart: Phase 1 — Core Framework Validation

**Prerequisites**: CMake 3.20+, GCC, C11 compiler, address sanitizer support

## Setup

```bash
cd firmware
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug \
         -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
make -j$(nproc)
```

## Build Targets

| Target | Description |
|---|---|
| `core` | Static library with all core modules |
| `linux_sim` | Linux simulator executable |
| `test_event_queue` | Event queue unit tests |
| `test_scheduler` | Scheduler unit tests |
| `test_system_fsm` | FSM unit tests |
| `test_mode_guard` | Mode guard unit tests |
| `test_data_repository` | Data repository unit tests |
| `test_linux_simulation` | End-to-end simulation scenario test |

## Run Tests

```bash
# All tests
ctest --output-on-failure

# Individual
./test_event_queue
./test_scheduler
./test_system_fsm
./test_mode_guard
./test_data_repository
./test_linux_simulation
```

## Run Simulator

```bash
./linux_sim

# Expected output (example):
# [INIT] Starting boot...
# [INIT] Core ready, transitioning to NORMAL
# [NORMAL] All services active
# [LOW_POWER] No blockers, entering STOP 2
# [WAKE] RTC alarm, resuming...
# [NORMAL] Resumed normal operation
```

## Validation Scenarios

### 1. Boot → NORMAL
- Inject `EVT_INIT_COMPLETED` with `core_ready=true`
- Expected: mode = NORMAL, transition_sequence incremented

### 2. Boot → RECOVERY (recoverable failure)
- Inject `EVT_RECOVERABLE_INIT_FAILURE` with `recovery_can_run=true`
- Expected: mode = RECOVERY

### 3. Boot → ERROR (critical failure)
- Inject `EVT_CRITICAL_INIT_FAILURE`
- Expected: mode = ERROR

### 4. NORMAL → LOW_POWER → WAKE → NORMAL
- Inject `EVT_LOW_POWER_REQUEST` with no blockers, wake armed
- Expected: mode = LOW_POWER
- Inject `EVT_WAKE` with valid wake reason
- Expected: mode = NORMAL

### 5. Blocked low-power
- Inject `EVT_LOW_POWER_REQUEST` with blocker present
- Expected: mode stays NORMAL, diagnostic updated

### 6. Critical event preempts low-power
- Post `EVT_CRITICAL_ERROR` and `EVT_LOW_POWER_REQUEST` together
- Expected: mode = ERROR (critical wins)

### 7. Snapshot double-buffer
- Publish flow result, acquire read handle
- Expected: snapshot contains consistent data across multiple reads
- Writer swap during active read: reader sees original version until release

### 8. Stale event rejection
- Post completion with old source_generation
- Expected: event rejected, no side effect, diagnostic counter incremented

### 9. Scheduler anchored periodic
- Schedule job at period P, advance virtual time
- Expected: deadlines at anchor+P, anchor+2P, ... (no drift)

### 10. Queue overflow
- Fill queue beyond capacity
- Expected: backpressure for config events; overflow escalation for critical
