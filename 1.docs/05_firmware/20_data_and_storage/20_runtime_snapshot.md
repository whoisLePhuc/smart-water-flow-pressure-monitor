---
document_id: FW-DATA-020
title: Runtime Snapshot
status: DRAFT
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-DATA-003
  - DEC-ARCH-006
  - DEC-MEAS-004
---

# Runtime Snapshot

## 0. Trạng thái triển khai tại firmware baseline

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- Implementation status: **IMPLEMENTED CORE / PARTIAL PRODUCT MODEL**
- Đã có: `RuntimeSnapshot`, double-buffer `DataRepository`, atomic active index/version, snapshot copy và typed `RepoWriteTxn`.
- Chưa có đầy đủ: mọi metadata/acceptance field của normative model, config snapshot và publication binding cho toàn bộ service.

## 1. Mục đích và phạm vi

Tài liệu định nghĩa view runtime nhất quán mà telemetry, storage, display và diagnostics được phép đọc. Nó hiện thực hướng thiết kế của [Data Model and Ownership](../00_core/04_data_model_and_ownership.md), không thay thế domain type.

## 2. Canonical ownership

| Thành phần | Quyền |
|---|---|
| `DataRepository` | Sở hữu hai buffer và version |
| `RepoWriteTxn` | Single writer của inactive buffer trong một transaction |
| Service/facade | Ghi typed field qua transaction |
| Consumer | Chỉ nhận bản copy immutable theo logical read |

Reader không giữ con trỏ tới internal buffer. API chuẩn là `data_repository_snapshot_copy()`; compatibility snapshot handle/accept API không được khôi phục.

## 3. Publication sequence

1. Reader input snapshot được copy nếu compute cần trạng thái trước đó.
2. Writer gọi `txn_init()` và `txn_begin()`.
3. Các typed writer cập nhật flow, pressure, temperature, volume, leak, mode hoặc power.
4. Failure làm `txn_abort()`.
5. Có field thay đổi thì `txn_commit()` atomically đổi active buffer và tăng version đúng một lần.
6. Không có field thay đổi thì abort, không tạo version rỗng.

Theo `DEC-DATA-003`, một accepted source event publish tối đa một final snapshot trong cùng event-loop turn.

## 4. Requirements

| ID | Requirement |
|---|---|
| FW-SNAP-REQ-001 | Snapshot consumer MUST không thấy partial update. |
| FW-SNAP-REQ-002 | Reader MUST capture một logical copy; không dùng mutable internal address. |
| FW-SNAP-REQ-003 | Commit thành công MUST tăng version đúng một lần. |
| FW-SNAP-REQ-004 | Abort/failure MUST giữ active snapshot và version cũ. |
| FW-SNAP-REQ-005 | Invalid/unavailable MUST khác numeric zero hợp lệ. |
| FW-SNAP-REQ-006 | Nested measurement MUST giữ sample time/status riêng. |
| FW-SNAP-REQ-007 | Thêm battery field MUST đi qua domain type, snapshot field, typed writer và tests. |
| FW-SNAP-REQ-008 | ISR/callback MUST NOT publish product snapshot trực tiếp. |

## 5. Battery extension

Canonical path: `AdcPort` → `PowerService`/`PowerConverter` → `PowerFacade` → `txn_write_power()` → `PowerSnapshot`. Baseline đã có đường này; power facade hiện dùng transaction riêng, chưa nằm trong MeasurementManager shared cycle.

## 6. Error và concurrency

Single-writer guard reject concurrent begin. Checked size/field mask bảo vệ typed writes. Queue/event failure sau commit không rollback snapshot; producer phải ghi diagnostic/status riêng. Atomic primitive trên STM32 cần toolchain/platform verification.

## 7. Verification

- Source: `src/infrastructure/repositories/runtime_snapshot.h`, `data_repository.*`, `repo_transaction.*`.
- Tests: `test_data_repository.c`, `test_repo_transaction.c`, `test_power_e2e.c`.
- Acceptance: copy consistency, begin contention, commit/abort/version, every typed field và power publication.


