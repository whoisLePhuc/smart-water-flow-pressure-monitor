---
document_id: FW-PLAT-052
title: STM32 Platform Backend
status: DRAFT
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-HW-006
  - DEC-HW-007
  - DEC-PWR-001
---

# STM32 Platform Backend

## 0. Trạng thái triển khai tại firmware baseline

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- Implementation status: **PARTIAL — SYNCHRONOUS ADC ADAPTER ONLY**
- Đã có: `adc_port_stm32` với injected HAL operation table và contract test.
- Chưa có: board HAL composition, async ADC/DMA completion, SPI/GPIO MAX35103, I2C ZSSC/FRAM, UART modem/BLE, LCD, watchdog, STOP2.
- Quan trọng: ADC adapter hiện gọi `configure → start → poll(timeout) → read → stop` đồng bộ. Nó không phải callback/DMA backend.

## 1. Current ADC contract

Board supplies HAL handle, operation table, battery channel và timeout. Adapter maps HAL status sang PortStatus và trả raw 16-bit count. `PowerConverter` mới chuyển raw count thành mV/health; threshold không thuộc adapter.

## 2. Runtime risk

`power_facade_process_sample()` gọi `adc_port_read()` trong event handler. Vì implementation dùng blocking poll, worst-case event-loop stall bằng ADC timeout cộng HAL overhead. Trước release phải đo/bound hoặc refactor async completion.

## 3. Target backend requirements

| ID | Requirement |
|---|---|
| FW-STM32-REQ-001 | Core/service MUST không include STM32 HAL. |
| FW-STM32-REQ-002 | Board context/handle lifetime MUST do composition sở hữu. |
| FW-STM32-REQ-003 | Blocking operation MUST có documented bounded WCET; preferred path là async event. |
| FW-STM32-REQ-004 | DMA/IRQ completion MUST correlate generation/operation. |
| FW-STM32-REQ-005 | ZSSC và FRAM MUST dùng một I2cBusManager theo `DEC-HW-006`. |
| FW-STM32-REQ-006 | STOP2/wake MUST theo `DEC-HW-007`. |
| FW-STM32-REQ-007 | Battery limits MUST từ validated config; `DEC-PWR-001` còn OPEN. |
| FW-STM32-REQ-008 | Static RAM/stack/flash budget MUST measured. |
| FW-STM32-REQ-009 | HAL error MUST map rõ, không leak raw enum lên domain. |
| FW-STM32-REQ-010 | Cross-build và HIL MUST là release gate. |

## 4. Porting order

Monotonic clock/event wake → ADC battery vertical slice → FRAM/boot restore → MAX SPI/GPIO → ZSSC shared I2C → modem/BLE UART → LCD → watchdog/STOP2.

## 5. Verification

Current evidence: `test_stm32_adc_adapter.c`. Required: cross-compile, WCET, timeout/error mapping, async race, shared bus, wake/resume, HIL and memory map.


