---
document_id: FW-DATA-021
title: Configuration Management
status: DRAFT
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-ARCH-007
  - DEC-HW-001
  - DEC-LEAK-001
  - DEC-PWR-001
  - DEC-DATA-004
---

# Configuration Management

## 0. Trạng thái triển khai tại firmware baseline

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- Implementation status: **PARTIAL**
- Đã có: `SensorProfile`, profile validation, `LeakConfig`, `PowerConfig` và default/semantic checks ở từng service.
- Chưa có: config repository/version, PendingConfig transaction, canonical persistent config codec, atomic activation và BLE command binding.

## 1. Mục tiêu

Configuration management tách compile-time safety envelope, product variant, per-device calibration và bounded runtime configuration. Driver không sở hữu product threshold. `DEC-ARCH-007` yêu cầu apply acknowledgement theo service/version.

## 2. Configuration layers

| Layer | Ví dụ | Mutability |
|---|---|---|
| Build/board | MCU, enabled peripheral, pin/channel | Build-time |
| Product variant | sensor compatibility, safe range | Release controlled |
| Device calibration | coefficients/fingerprint | Factory controlled |
| Runtime config | period, reporting, leak/power thresholds | Validated transaction |

`DEC-HW-005` và `DEC-PWR-001` còn OPEN; battery threshold/hysteresis phải giữ trong validated profile/config và không được tuyên bố đã qualified.

## 3. Target transaction

Receive candidate → authenticate/authorize → decode/schema check → semantic/cross-field validation → build immutable PendingConfig → persist/verify → apply tại safe boundary → collect per-service result → atomically publish ActiveConfig version.

Mọi failure giữ active version cũ. Service không được quan sát một phần config mới.

## 4. Requirements

| ID | Requirement |
|---|---|
| FW-CFG-REQ-001 | External input MUST không ghi trực tiếp active struct. |
| FW-CFG-REQ-002 | Candidate MUST có schema, config version và transaction ID. |
| FW-CFG-REQ-003 | Validate MUST gồm type, range, cross-field và hardware compatibility. |
| FW-CFG-REQ-004 | Apply MUST diễn ra tại service safe boundary. |
| FW-CFG-REQ-005 | Failure MUST rollback logical activation và giữ previous version. |
| FW-CFG-REQ-006 | Driver MUST không chứa leak/power/product thresholds. |
| FW-CFG-REQ-007 | Secret/credential MUST không vào RuntimeSnapshot/general diagnostic. |
| FW-CFG-REQ-008 | Unknown schema MUST bị reject rõ ràng. |
| FW-CFG-REQ-009 | Profile change ảnh hưởng leak evidence MUST reset theo `DEC-LEAK-001`. |
| FW-CFG-REQ-010 | Config persistence MUST dùng canonical codec/A-B recovery. |

## 5. Current code mapping

- `src/services/configuration/sensor_profile.h`
- `src/services/configuration/profile_validation.c`
- `src/services/leak/leak_config.*`
- `src/domain/power/power_config.h`

Các thành phần này chỉ là validator/domain foundation, chưa tạo end-to-end transaction.

## 6. Verification và exit criteria

Cần unit tests invalid/cross-field, integration apply success/rollback, power-loss giữa persist/apply, boot restore/migration, two-variant compatibility và authorization tests. Chỉ chuyển status sang Implemented khi AppComposition sở hữu config repository và service acknowledgement được kiểm chứng.


