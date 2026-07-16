---
document_id: FW-IF-031
title: BLE Command and Configuration Binding
status: DRAFT
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-ARCH-007
  - DEC-MODE-001
  - DEC-SVC-001
  - DEC-HW-002
---

# BLE Command and Configuration Binding

## 0. Trạng thái triển khai tại firmware baseline

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- Implementation status: **PLANNED**
- Đã có: ModeGuard, FSM và profile validators làm nền tảng.
- Chưa có/Chưa hoàn tất: BLE command parser/router, authenticated session và config transaction end-to-end.
- Quy ước đọc: requirement bên dưới là normative contract; phần chưa có source/composition/test không được xem là capability hiện hành.

## 1. Command lifecycle

Decode → schema/version check → authenticate role/session → correlate operation → ModeGuard → execute use case hoặc build PendingConfig → persist/verify/apply → collect per-service acknowledgement → encode response.

## 2. Command classes

| Class | Policy |
|---|---|
| Read identity/status | Allowed theo mode và redaction |
| Set time | Controlled validation và time-service owner |
| Config candidate | Full transaction; không direct write |
| Enter SERVICE | `DEC-SVC-001` authentication/timeout |
| Reset volume/factory data | Privileged, idempotent operation ID |
| OTA/DFU | Out of scope MVP |

## 3. Requirements

| ID | Requirement |
|---|---|
| FW-BLECMD-REQ-001 | Mỗi request MUST có protocol version và correlation ID. |
| FW-BLECMD-REQ-002 | Unsupported command/version MUST trả explicit error. |
| FW-BLECMD-REQ-003 | Mutating command MUST authenticated và mode-guarded. |
| FW-BLECMD-REQ-004 | Duplicate operation ID MUST không lặp side effect. |
| FW-BLECMD-REQ-005 | Config response MUST chứa config version + per-service result. |
| FW-BLECMD-REQ-006 | Apply failure MUST giữ ActiveConfig cũ. |
| FW-BLECMD-REQ-007 | Session timeout/disconnect MUST cancel hoặc hoàn tất theo documented policy. |
| FW-BLECMD-REQ-008 | Factory/destructive command MUST không có implicit fallback. |

## 4. Error model

Phân biệt malformed, unsupported, unauthorized, invalid state, validation failure, busy, persistence failure, apply failure và internal error. Không trả raw pointer/secret/driver status trực tiếp.

## 5. Verification

Tests cần cover role matrix, INIT limited access, SERVICE timeout, duplicate command, interrupted config persistence, partial apply rollback, reset authorization và response correlation.


