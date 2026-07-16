---
document_id: FW-IF-032
title: 4G Modem Integration
status: DRAFT
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-HW-003
  - DEC-COM-001
  - DEC-COM-002
  - DEC-COM-003
  - DEC-COM-004
---

# 4G Modem Integration

## 0. Trạng thái triển khai tại firmware baseline

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- Implementation status: **PARTIAL SERVICE MODEL / PLANNED MODEM BACKEND**
- Đã có: `ReportingSchedule`, `TelemetryQueue`, `CellularDelivery` và telemetry builder.
- Chưa có/Chưa hoàn tất: EC200U-CN UART/AT port/driver, network/session state machine và MQTT/HTTP adapter.
- Quy ước đọc: requirement bên dưới là normative contract; phần chưa có source/composition/test không được xem là capability hiện hành.

## 1. Boundary

Modem backend sở hữu UART/AT command transport, SIM/network registration và socket/session operations. `CellularDelivery` sở hữu delivery/retry policy. Telemetry builder/queue không gọi modem trực tiếp.

## 2. Runtime flow

Report slot → stable snapshot → immutable telemetry record → queue → delivery submit → modem asynchronous operation → correlated completion → ACK/remove hoặc retry deadline.

## 3. Requirements

| ID | Requirement |
|---|---|
| FW-MODEM-REQ-001 | AT operation MUST asynchronous, correlated và bounded. |
| FW-MODEM-REQ-002 | Parser MUST handle partial/combined/unsolicited lines. |
| FW-MODEM-REQ-003 | Stale completion MUST bị reject bằng generation. |
| FW-MODEM-REQ-004 | Retry MUST theo `DEC-COM-003`, không busy-wait. |
| FW-MODEM-REQ-005 | Remove queue item MUST theo `DEC-COM-002`. |
| FW-MODEM-REQ-006 | Credentials MUST owned outside general snapshot/log. |
| FW-MODEM-REQ-007 | Modem power-cycle MUST invalidate in-flight operation. |
| FW-MODEM-REQ-008 | Offline state MUST là connectivity status, không primary mode. |
| FW-MODEM-REQ-009 | Backoff/deadline MUST dùng monotonic time. |
| FW-MODEM-REQ-010 | Peak-current/power gating MUST chờ `DEC-HW-005` qualification. |

## 4. MVP scope

MQTT QoS 1 hoặc HTTP POST versioned JSON theo communication specification. Automatic protocol failover, persistent offline queue và generic remote command ngoài baseline.

## 5. Verification

Linux fake modem cần normal, partial response, URC interleave, timeout, reconnect, duplicate ACK, power cycle và offline TTL scenarios. STM32 cần UART/DMA contract và HIL qualification.


