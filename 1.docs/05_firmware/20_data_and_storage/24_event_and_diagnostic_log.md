---
document_id: FW-DATA-024
title: Event and Diagnostic Log
status: DRAFT
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-DIAG-001
  - DEC-ERR-005
  - DEC-DATA-004
---

# Event and Diagnostic Log

## 0. Trạng thái triển khai tại firmware baseline

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- Implementation status: **PLANNED**
- Đã có nền tảng: structured statuses, event IDs, queue/scheduler counters và normalized simulation trace.
- Chưa có: diagnostic log service, 32-bit error registry implementation đầy đủ, persistent log schema/retention/upload.
- `DEC-DIAG-001` vẫn OPEN; numeric retention/coalescing/upload không được tự chốt trong firmware doc.

## 1. Phạm vi

Log có cấu trúc ghi boot/reset reason, mode transition, sensor/transport failure, queue overflow, storage recovery, config apply và assertion outcome. Nó không thay thế per-service status hoặc dùng chuỗi printf làm wire/storage schema.

## 2. Entry model mục tiêu

| Field | Contract |
|---|---|
| error/event code | Stable registry theo `DEC-ERR-005` |
| monotonic time | Bắt buộc |
| wall time | Optional, có validity |
| severity/source | Enum ổn định |
| correlation/generation | Khi liên quan operation |
| payload | Bounded, typed, không secret |

## 3. Requirements

| ID | Requirement |
|---|---|
| FW-DIAG-REQ-001 | ISR MUST chỉ tăng counter hoặc enqueue bounded evidence. |
| FW-DIAG-REQ-002 | Credentials, keys và raw secret MUST không xuất hiện trong log. |
| FW-DIAG-REQ-003 | Overflow MUST có policy và lost-count. |
| FW-DIAG-REQ-004 | Persistent encoding MUST versioned, length-delimited và integrity checked. |
| FW-DIAG-REQ-005 | Logging failure MUST không chặn measurement/recovery. |
| FW-DIAG-REQ-006 | Duplicate fault MAY coalesce only by decided policy. |
| FW-DIAG-REQ-007 | Consumer MUST phân biệt occurrence time và upload time. |
| FW-DIAG-REQ-008 | Unknown code MUST vẫn decode envelope an toàn. |

## 4. Proposed boundaries

`DiagnosticSink` nhận typed entry; RAM ring giữ bounded records; optional storage adapter dùng record class riêng; telemetry export tạo immutable diagnostic view. Không cho module gọi F-RAM/modem trực tiếp.

## 5. Verification và gate

Trước triển khai phải chốt `DEC-DIAG-001`. Tests cần cover overflow, redaction, coalescing, power loss, schema migration, unknown code và logging-backpressure. Cho đến lúc đó tài liệu này là normative plan, không phải capability.


