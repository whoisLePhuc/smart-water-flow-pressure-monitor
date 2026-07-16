---
document_id: FW-DATA-023
title: Telemetry Queue
status: DRAFT
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-COM-002
  - DEC-COM-003
  - DEC-COM-004
  - DEC-SCHED-003
---

# Telemetry Queue

## 0. Trạng thái triển khai tại firmware baseline

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- Implementation status: **IMPLEMENTED MVP RAM QUEUE / PARTIAL TRANSPORT**
- Đã có: `TelemetryQueue`, `CellularDelivery`, reporting schedule, telemetry builder và unit/integration tests.
- Chưa có: modem/AT backend thật, MQTT/HTTP wire adapter và durable offline queue.

## 1. MVP contract

Theo `DEC-COM-004`, MVP dùng static RAM FIFO 64 record, một in-flight, TTL 24 giờ và drop-oldest khi full. Theo `DEC-SCHED-003`, record được tạo theo scheduled slot; leak transition không tự tạo immediate telemetry.

## 2. Ownership và lifecycle

Builder tạo immutable record từ một stable RuntimeSnapshot. Queue sở hữu bản copy sau enqueue. Delivery mượn front record nhưng không sửa payload. Chỉ transport acknowledgement theo `DEC-COM-002` mới remove record. Reset làm mất queue là limitation MVP đã chấp nhận.

## 3. State model

EMPTY → QUEUED → IN_FLIGHT → ACKED/RETRY_WAIT/DROPPED. Retry là cùng record identity, không build lại từ snapshot. Fixed retry 30 giây, tối đa ba consecutive retry theo `DEC-COM-003`.

## 4. Requirements

| ID | Requirement |
|---|---|
| FW-TQ-REQ-001 | Enqueue MUST copy immutable record và preserve identity. |
| FW-TQ-REQ-002 | Queue full MUST áp dụng documented drop-oldest và tăng diagnostic counter. |
| FW-TQ-REQ-003 | Exactly one record MAY be in-flight. |
| FW-TQ-REQ-004 | Retry MUST không tạo duplicate record identity. |
| FW-TQ-REQ-005 | Remove MUST chỉ sau PUBACK/HTTP 2xx equivalent. |
| FW-TQ-REQ-006 | Expired record MUST không được gửi. |
| FW-TQ-REQ-007 | Queue/delivery MUST không block event loop. |
| FW-TQ-REQ-008 | Wall-clock correction MUST không phá monotonic retry deadline. |

## 5. Current mapping

`src/services/connectivity/telemetry_queue.*`, `cellular_delivery.*`, `reporting_schedule.*`; payload types/builder tại `src/protocols/telemetry`.

## 6. Error và recovery

Offline giữ eligible records đến TTL/capacity. Queue overflow, retry exhausted, invalid ack và stale completion phải observable. Automatic MQTT↔HTTP failover và persistent replay không thuộc baseline.

## 7. Verification

Tests: telemetry queue, delivery service, reporting schedule, telemetry builder và reporting E2E. Bổ sung modem fake/contract tests trước khi đổi transport status sang Implemented.


