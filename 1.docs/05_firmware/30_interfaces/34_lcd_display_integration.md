---
document_id: FW-IF-034
title: LCD Display Integration
status: DRAFT
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-HW-004
  - DEC-MEAS-004
---

# LCD Display Integration

## 0. Trạng thái triển khai tại firmware baseline

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- Implementation status: **PLANNED**
- Đã có: RuntimeSnapshot và facade/view boundaries làm nền tảng.
- Chưa có/Chưa hoàn tất: LCD hardware decision, display service/view model, driver, platform adapter và tests; `DEC-HW-004` vẫn OPEN.
- Quy ước đọc: requirement bên dưới là normative contract; phần chưa có source/composition/test không được xem là capability hiện hành.

## 1. Boundary

Display service chuyển stable snapshot + system mode thành bounded view model. LCD driver/adapter chỉ render/transfer. LCD không sở hữu canonical state và không đọc sensor driver.

## 2. Requirements

| ID | Requirement |
|---|---|
| FW-LCD-REQ-001 | Display MUST chỉ đọc published snapshot/view. |
| FW-LCD-REQ-002 | Invalid/stale/unavailable MUST có representation riêng. |
| FW-LCD-REQ-003 | Refresh MUST rate-limited và non-blocking. |
| FW-LCD-REQ-004 | Transfer callback MUST không chạy business logic. |
| FW-LCD-REQ-005 | Shared bus MUST đi qua owner/manager tương ứng. |
| FW-LCD-REQ-006 | DMA buffer MUST sống đến completion. |
| FW-LCD-REQ-007 | Mode/health indication MUST derived, không tự đổi FSM. |
| FW-LCD-REQ-008 | Unsupported hardware mapping MUST fail build/config validation. |

## 3. Open hardware dependency

Model, segment mapping, physical interface và timing thuộc `DEC-HW-004`. Firmware có thể định nghĩa view model trước, nhưng không chốt pin/bus/refresh numeric khi decision còn mở.

## 4. Verification

Linux text/segment renderer fake, snapshot-to-view golden tests, stale/invalid rendering, rapid update coalescing, bus busy và DMA completion race. HIL chỉ sau hardware decision.


