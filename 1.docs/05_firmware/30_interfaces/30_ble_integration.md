---
document_id: FW-IF-030
title: BLE Integration
status: DRAFT
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-SYS-001
  - DEC-HW-002
  - DEC-MODE-001
  - DEC-ARCH-008
---

# BLE Integration

## 0. Trạng thái triển khai tại firmware baseline

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- Implementation status: **PLANNED**
- Đã có: system decisions về vai trò BLE coprocessor và mode policy.
- Chưa có/Chưa hoàn tất: Không có BLE port, UART/AT driver, parser, service, GATT/command binding hoặc test trong baseline.
- Quy ước đọc: requirement bên dưới là normative contract; phần chưa có source/composition/test không được xem là capability hiện hành.

## 1. Responsibility boundary

nRF52810 là BLE coprocessor; STM32 firmware giao tiếp qua UART/AT contract riêng. BLE được phép tạo request, command hoặc PendingConfig, nhưng không sở hữu ActiveConfig, sensor, repository hay storage. OTA/DFU ngoài MVP theo `DEC-ARCH-008`.

## 2. Data flow

UART RX callback → bounded transport buffer → frame/parser → authenticated command event → mode guard/use case → response encoder → asynchronous UART TX. Callback không gọi repository/FSM/storage trực tiếp.

## 3. Requirements

| ID | Requirement |
|---|---|
| FW-BLE-REQ-001 | Transport frame MUST versioned, bounded và integrity checked. |
| FW-BLE-REQ-002 | RX callback MUST bounded và defer parsing. |
| FW-BLE-REQ-003 | Command MUST có correlation ID và explicit result. |
| FW-BLE-REQ-004 | BLE access trong INIT MUST tuân `DEC-MODE-001`. |
| FW-BLE-REQ-005 | Config MUST đi qua PendingConfig transaction. |
| FW-BLE-REQ-006 | Reset/service action MUST qua authorization + ModeGuard. |
| FW-BLE-REQ-007 | Notification MUST build từ stable snapshot/view. |
| FW-BLE-REQ-008 | Backpressure MUST không block event loop. |
| FW-BLE-REQ-009 | Secret MUST không vào snapshot/log. |
| FW-BLE-REQ-010 | Disconnect/retry MUST không duplicate command side effect. |

## 4. Dependencies and tests

Liên quan [BLE command/config binding](31_ble_command_and_config_binding.md), [Config Management](../20_data_and_storage/21_config_management.md) và communication contracts trong `1.docs/03_communication`. Exit criteria: port/adapter/parser, Linux fake coprocessor, authorization tests, fragmented frame, timeout, duplicate và queue-full scenarios.


