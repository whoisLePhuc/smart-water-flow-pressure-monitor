<p align="center">
  <img src="assets/logo.png" alt="Smart Ultrasonic Water Meter Logo" width="240">
</p>

<h1 align="center">Smart Water Flow and Pressure Monitor</h1>

<p align="center">
  <b>STM32-based water monitoring system</b><br>
  Ultrasonic flow · Temperature · Pressure · Leak detection · BLE · 4G telemetry
</p>

<p align="center">
  <img alt="Status" src="https://img.shields.io/badge/status-pre--bring--up%20Linux%20verified-blue">
  <img alt="MCU" src="https://img.shields.io/badge/MCU-STM32L433RCT6-green">
  <img alt="Flow" src="https://img.shields.io/badge/flow-MAX35103-orange">
  <img alt="Pressure" src="https://img.shields.io/badge/pressure-ZSSC3241-yellow">
  <img alt="License" src="https://img.shields.io/badge/license-MIT-lightgrey">
</p>

---

## Overview

**Smart Water Flow and Pressure Monitor** is an embedded-system project for:

* ultrasonic flow and temperature measurement,
* cumulative water-volume calculation,
* pressure monitoring,
* leak-condition detection,
* local LCD display,
* BLE configuration and service,
* and scheduled telemetry through a 4G connection.

The project follows a **documentation-first and simulation-first** development approach. Portable measurement and reliability paths are implemented and exercised on Linux; integration and verification on STM32 hardware remain separate milestones.

---

## System Baseline

| Block                | Selected baseline                      |
| -------------------- | -------------------------------------- |
| Main MCU             | `STM32L433RCT6`                        |
| Flow and temperature | `MAX35103`                             |
| Pressure             | Resistive pressure bridge + `ZSSC3241` |
| Persistent storage   | `FM24CL04B` F-RAM                      |
| Local connectivity   | `nRF52810` BLE coprocessor             |
| Remote connectivity  | `Quectel EC200U-CN` LTE Cat 1 bis      |
| Timekeeping          | STM32 internal RTC                     |
| Firmware model       | Cooperative event-driven runtime       |
| Low-power mode       | STM32 `STOP 2`                         |
| Validation target    | Linux simulation before STM32 bring-up |

BLE is used for local configuration and service. The 4G modem is used for scheduled telemetry and time synchronization.

OTA and generic remote configuration through 4G are outside the current MVP.

---

## Architecture

```text
MAX35103
  -> flow and temperature processing
  -> calibration
  -> volume accumulation

Pressure bridge + ZSSC3241
  -> pressure processing

Flow + volume + pressure + time
  -> leak detection
  -> RuntimeSnapshot
  -> LCD / telemetry / diagnostics

BLE
  -> configuration validation
  -> persistent commit
  -> controlled runtime apply

RTC
  -> reporting scheduler
  -> telemetry queue
  -> EC200U-CN
  -> remote server
```

Important design rules:

* Measurement logic does not depend on BLE, 4G or LCD.
* LCD and telemetry read a stable `RuntimeSnapshot`.
* Invalid or stale data does not update production volume or leak evidence.
* Persistent records use versioning, CRC and A/B slots.
* ZSSC3241 and F-RAM share one managed I2C bus.
* Interrupt handlers only capture minimal data and publish events.
* Communication and recovery flows must be non-blocking or bounded.
* Core firmware logic should remain independent of STM32 HAL.

---

## Repository Structure

```text
smart-water-flow-pressure-monitor/
├── 1.docs/                # System design and firmware architecture
│   ├── 00_overview/       # System behavior, decisions and traceability
│   ├── 01_principle/      # Measurement principles
│   ├── 02_hardware/       # Hardware design
│   ├── 05_firmware/       # Firmware architecture (00_core/, 50_platform/, etc.)
│   ├── 04_communication/  # BLE and 4G contracts
│   └── 08_simulation/     # Linux simulation design
├── 2.firmware/            # Firmware implementation (portable core + Linux simulation)
│   ├── include/           # Public headers
│   ├── src/               # Source modules
│   ├── apps/              # Simulator app
│   └── tests/               # Unit, integration, contract and system tests
├── 3.hardware/            # Hardware resources
├── 4.software/            # Host tools and scripts
├── 5.references/          # Datasheets and references
├── assets/
├── LICENSE
└── README.md
```

---

## Current Status

"Implemented", "integrated" and "verified" are tracked independently:

| Area | Implemented | Integrated | Linux verified | STM32 verified |
| --- | --- | --- | --- | --- |
| Event runtime, FSM, guards and action executor | Yes | Yes | Yes | No |
| I2C manager → ZSSC3241 → pressure snapshot | Yes | Yes | Yes | No |
| SPI manager, MAX35103 decoder and flow pipeline | Yes | Yes, with recorded completions | Yes | No |
| Flow → volume → leak atomic commit | Yes | Yes | Yes | No |
| F-RAM codec, true A/B rotation and restore | Yes | Yes, memory backend | Yes | No |
| STM32 ADC/I2C/SPI adapter contracts | Skeleton | Not board-bound | Compile checked | No |
| RTC, STOP 2 and watchdog ports | Contract only | No | Header checked | No |
| BLE, 4G and LCD product integration | Partial contracts | No | Partial | No |

See [`2.firmware/IMPLEMENTATION_STATUS.md`](2.firmware/IMPLEMENTATION_STATUS.md) for the verification boundary and remaining hardware work.

---

## Documentation

Recommended starting points:

1. [`1.docs/00_overview/README.md`](1.docs/00_overview/README.md)
2. [`1.docs/00_overview/00_open_questions_and_decisions.md`](1.docs/00_overview/00_open_questions_and_decisions.md)
3. [`1.docs/00_overview/11_firmware_implication.md`](1.docs/00_overview/11_firmware_implication.md)
4. [`1.docs/00_overview/12_system_traceability.md`](1.docs/00_overview/12_system_traceability.md)
5. [`1.docs/05_firmware/README.md`](1.docs/05_firmware/README.md)

Detailed requirements, decisions, timing behavior and interface contracts belong in `1.docs/`, not in the root README.

---

## Roadmap

```text
✅ Firmware architecture documentation (FW-CORE v0.2)
✅ Portable firmware core (event loop, FSM, scheduler, data model)
✅ Platform abstraction + Linux deterministic backend
✅ SPI / I2C / GPIO platform providers
✅ MAX35103 + ZSSC3241 + F-RAM device peers
✅ Simulation harness + scenario runner
✅ Unit, integration, contract and deterministic scenario tests
✅ Temperature processing (calibration, RTD, linearisation)
✅ Flow processing (TOF, temperature pairing, forward/reverse)
✅ Pressure processing (ZSSC3241, endpoint mapping, calibration)
✅ Volume accumulation (forward/reverse, remainder carry, admission gate)
✅ Persistent A/B storage (CRC-32/ISO-HDLC, async StorageService, F-RAM driver)
✅ Boot restore and corruption recovery (newest-valid selection, torn-record rejection)
✅ Leak detection (continuous-flow, burst, pressure diagnostic, config validation)
✅ Reporting schedule (two-window, TimeService, TelemetryBuilder, 64-slot queue, delivery)

🟡 STM32 platform port contracts and ADC/I2C/SPI adapter skeletons
⏳ Board binding for timer / GPIO / RTC / F-RAM / STOP 2 / watchdog
⏳ BLE and 4G communication integration
⏳ Hardware bring-up and qualification
```

---

## License

This project is licensed under the [MIT License](LICENSE).
