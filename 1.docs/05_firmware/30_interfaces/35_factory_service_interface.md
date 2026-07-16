---
document_id: FW-IF-035
title: Factory Service Interface
status: DRAFT
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-SVC-001
  - DEC-HW-008
  - DEC-ERR-005
---

# Factory Service Interface

## 0. Trạng thái triển khai tại firmware baseline

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- Implementation status: **PLANNED**
- Đã có: FSM, ModeGuard, validators và storage foundation.
- Chưa có/Chưa hoàn tất: Factory protocol/service, authorization roles, service UART decision và provisioning/audit tests.
- Quy ước đọc: requirement bên dưới là normative contract; phần chưa có source/composition/test không được xem là capability hiện hành.

## 1. Scope

Factory/service operations gồm identity, self-check, sensor diagnostics, calibration/profile provisioning và guarded reset. Đây là use case có policy, không phải đường gọi driver tùy ý.

## 2. Session model

Entry qua authenticated BLE hoặc debug/service transport theo `DEC-SVC-001`; role/allowlist, inactivity timeout và explicit exit. Dedicated service UART vẫn OPEN theo `DEC-HW-008`.

## 3. Requirements

| ID | Requirement |
|---|---|
| FW-FACT-REQ-001 | Session MUST authenticated, role-based và bounded. |
| FW-FACT-REQ-002 | Command MUST versioned/correlated/auditable. |
| FW-FACT-REQ-003 | Production measurement side effect MUST obey SERVICE policy. |
| FW-FACT-REQ-004 | Calibration provisioning MUST validate, persist, read back và atomically activate. |
| FW-FACT-REQ-005 | ZSSC NVM write MUST require stable power, authorization và verification. |
| FW-FACT-REQ-006 | Destructive reset MUST explicit, idempotent và guarded. |
| FW-FACT-REQ-007 | Production build MUST có policy enable/disable transport. |
| FW-FACT-REQ-008 | Secret output MUST redacted. |
| FW-FACT-REQ-009 | Session loss MUST leave system in deterministic state. |

## 4. Error and recovery

Interrupted provisioning giữ active calibration cũ; candidate chưa verify không được active. Transport loss không tự commit. Audit failure không được che command failure.

## 5. Verification

Role matrix, timeout, invalid profile, interrupted write, readback mismatch, duplicate reset, production-disable và fresh measurement requirement after provisioning.


