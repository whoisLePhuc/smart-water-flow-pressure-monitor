# Firmware — Smart Water Flow and Pressure Monitor

Portable C11 firmware for an STM32L433-based water flow and pressure monitor.
The repository uses Linux simulation and contract tests before board bring-up.

The precise implementation and verification boundary is recorded in
[`IMPLEMENTATION_STATUS.md`](IMPLEMENTATION_STATUS.md); the artifact build
evidence is in [`VERIFICATION_REPORT.md`](VERIFICATION_REPORT.md).

## Build and test

Requirements: CMake 3.20+, GCC or Clang with C11 support.

From the repository root:

```bash
cd 2.firmware
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
bash scripts/check_architecture.sh --enforce
```

Sanitizer build:

```bash
cd 2.firmware
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-san --parallel
ctest --test-dir build-san --output-on-failure
```

## Implemented portable paths

- One shared priority/FIFO I2C manager for pressure and F-RAM clients, plus the
  SPI transaction state machine, with manager-owned identity, timeout,
  cancellation, stale-completion and recovery handling.
- ZSSC3241 command/read/status/U24 decoding and pressure publication.
- MAX35103 TOF register-frame decoding, temperature pairing, physical flow
  conversion and freshness/production gates.
- One atomic repository commit for `FlowResult`, `VolumeState` and
  `LeakDetectionResult`.
- An asynchronous FM24CL04B driver with `0x50`/`0x51` page mapping and bounded
  chunking, connected through `StoragePort` to the shared I2C manager.
- CRC-protected F-RAM records, asynchronous invalidate/write/readback/commit
  sequence, true A/B rotation and newest-valid boot restore.
- Evidence-backed FSM guards, bounded action execution, loop budgets and an
  ISR critical-section contract.
- Linux deterministic providers, peers, scenarios and regression tests.

## STM32 boundary

`src/platform/stm32/adapters` contains compile-ready ADC, asynchronous I2C and
SPI adapters whose HAL operations are supplied by board code. Port contracts
also exist for RTC, GPIO IRQ, UART, STOP 2 and watchdog. Pin assignments,
clocks, DMA/IRQ wiring, exact peripheral handles, STOP 2 behavior and watchdog
timing still require board bring-up and cannot be verified on Linux.

Recommended bring-up order:

1. monotonic timer and ISR event queue;
2. battery ADC;
3. I2C and ZSSC3241;
4. SPI and MAX35103;
5. RTC and F-RAM;
6. STOP 2 and watchdog;
7. BLE, 4G and LCD after measurement and storage are stable.

## Source layout

```text
2.firmware/
├── apps/                    Linux simulator entry point
├── cmake/                   compiler and sanitizer policy
├── scripts/                 architecture and verification checks
├── src/
│   ├── app/                 composition, modes and guards
│   ├── domain/              portable data types and policies
│   ├── drivers/             MAX35103, ZSSC3241 and F-RAM drivers
│   ├── infrastructure/      queues, buses, scheduler and repositories
│   ├── platform/            Linux backend and STM32 adapters
│   ├── ports/               narrow platform contracts
│   ├── protocols/           storage and telemetry codecs
│   └── services/            measurement, processing, volume, leak, storage
└── tests/                   unit, integration, contract and system tests
```
