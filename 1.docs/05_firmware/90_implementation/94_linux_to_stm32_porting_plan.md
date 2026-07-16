---
document_id: FW-IMPL-094
title: Linux to STM32 Porting Plan
status: ACTIVE
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-HW-006
  - DEC-HW-007
  - DEC-ARCH-005
---

# Linux to STM32 Porting Plan

## 0. Baseline

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- Linux simulation backend: implemented.
- STM32 backend: synchronous ADC adapter contract only.
- Porting MUST giữ nguyên domain/service/protocol semantics; HAL changes ở platform composition/adapters.

## 1. Preconditions

Pin/clock/memory map, toolchain/linker, interrupt budget, I2C/SPI/UART electrical assumptions và open hardware gates được ghi rõ. Không dùng Linux peer timing làm bằng chứng electrical.

## 2. Ordered vertical slices

1. Monotonic clock, event wake và system control/reset reason.
2. ADC battery: board mapping, reference calibration, WCET/async decision.
3. F-RAM: shared I2C owner, canonical codec, A/B restore.
4. MAX35103: SPI/GPIO INT, correlated operation, raw payload.
5. ZSSC3241: shared I2C, one-shot EOC/poll, raw payload.
6. Full measurement compute/shared transaction.
7. UART modem/BLE adapters.
8. Watchdog, boot self-check và STOP2 wake/resume.
9. LCD sau hardware decision.

## 3. Adapter gate

| Gate | Evidence |
|---|---|
| Dependency | HAL include chỉ trong STM32 platform |
| Ownership | Static context/buffer lifetime documented |
| Concurrency | token/generation, timeout/completion race |
| Timing | WCET/deadline/ISR budget |
| Error | HAL→PortStatus→domain mapping |
| Tests | host contract + target/HIL |
| Resources | stack/static RAM/flash report |

## 4. ADC special action

Current adapter uses blocking `poll(timeout)`. Trước production phải chọn và document:

- bounded polling được chấp nhận với measured WCET nhỏ hơn event-loop budget; hoặc
- async start + IRQ/DMA completion event.

Không được gọi nó là callback/non-blocking khi chưa refactor.

## 5. Cross-backend parity

Cùng scenario/golden vector cho numeric/domain logic. Linux validates deterministic semantics; HIL validates HAL, timing, interrupts và wake. Divergence phải được ghi như platform contract, không fork business algorithm.

## 6. Exit criteria

Cross-build clean, architecture check, all host tests, target contract tests, HIL normal/fault/wake, memory/timing budget và traceability cập nhật.


