---
document_id: FW-IF-033
title: Telemetry Payload Binding
status: DRAFT
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-COM-001
  - DEC-SCHED-003
  - DEC-MEAS-004
---

# Telemetry Payload Binding

## 0. Trạng thái triển khai tại firmware baseline

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- Implementation status: **IMPLEMENTED BUILDER / PARTIAL WIRE BINDING**
- Đã có: `TelemetryRecord`, views và `TelemetryBuilder` cùng unit/reporting tests.
- Chưa có/Chưa hoàn tất: Canonical MQTT/HTTP JSON encoder, full schema governance và modem transport binding.
- Quy ước đọc: requirement bên dưới là normative contract; phần chưa có source/composition/test không được xem là capability hiện hành.

## 1. Source contract

Builder nhận một stable RuntimeSnapshot/view và ghi immutable record có `source_snapshot_version`. Nó không đọc repository nhiều lần, không gọi queue/modem và không dùng driver raw layout.

## 2. Field policy

Mỗi measurement field phải có unit, validity/status và schema rule. Numeric zero khác unavailable. Battery voltage chỉ thêm khi `PowerSnapshot` mapping, backward compatibility và server schema đã chốt.

## 3. Requirements

| ID | Requirement |
|---|---|
| FW-TEL-REQ-001 | Record MUST derive từ đúng một snapshot version. |
| FW-TEL-REQ-002 | Wire encoding MUST field-by-field; không memcpy C struct. |
| FW-TEL-REQ-003 | Schema/version MUST explicit. |
| FW-TEL-REQ-004 | Missing/invalid MUST không bị encode thành valid zero. |
| FW-TEL-REQ-005 | Record identity MUST stable qua retry. |
| FW-TEL-REQ-006 | Unknown optional field SHOULD backward-compatible. |
| FW-TEL-REQ-007 | Secret/internal diagnostic MUST không vào normal telemetry. |
| FW-TEL-REQ-008 | Overflow/encoding failure MUST reject record, không truncate âm thầm. |
| FW-TEL-REQ-009 | Leak transition MUST không tự tạo immediate report trong MVP. |

## 4. Current mapping

`src/protocols/telemetry/telemetry_record.h`, `telemetry_views.h`, `telemetry_builder.*`; consumers tại connectivity services.

## 5. Verification

Golden JSON/wire vectors, invalid field combinations, min/max numeric, schema backward compatibility, stable retry bytes và server-side dedup identity.


