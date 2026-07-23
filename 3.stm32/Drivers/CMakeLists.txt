# =============================================================================
# Drivers - Aggregate target
#
# Mỗi driver con là một static library riêng.
# Target `drivers` (INTERFACE) gom nhóm để module khác chỉ cần link với
# `drivers` thay vì phải biết từng driver cụ thể.
#
# Khi thêm driver mới:
#   1. Tạo thư mục drivers/<driver_name>/
#   2. Thêm add_subdirectory(<driver_name>)
#   3. Thêm target_link_libraries vào target `drivers` bên dưới
# =============================================================================

add_subdirectory(FM24CL04B)
add_subdirectory(MAX35103)
add_subdirectory(ZSSC3241)
# add_subdirectory(Battery)
# add_subdirectory(EC200U)
# add_subdirectory(nRF52810)

# ── Aggregate target ──
add_library(drivers INTERFACE)

target_link_libraries(drivers
    INTERFACE
        driver_fram
        driver_max35103
        driver_zssc3241
)
