# =============================================================================
# Tests
#
# Các target test được build vào firmware khi bật flag FIRMWARE_BUILD_TESTS.
# Sử dụng: cmake -DFIRMWARE_BUILD_TESTS=ON ..
#
# Khi thêm test mới:
#   1. Tạo thư mục Core/Tests/<test_name>/
#   2. Thêm add_subdirectory(<test_name>)
# =============================================================================
option(FIRMWARE_BUILD_TESTS "Build test targets into firmware" OFF)
option(FIRMWARE_BUILD_TESTS_MAX35103 "Build MAX35103 test target into firmware" OFF)
option(FIRMWARE_BUILD_TESTS_FM24CL04B "Build FM24CL04B test target into firmware" OFF)
option(FIRMWARE_BUILD_TESTS_ZSSC3241 "Build ZSSC3241 test target into firmware" OFF)

if(FIRMWARE_BUILD_TESTS)
    if(FIRMWARE_BUILD_TESTS_FM24CL04B)
        add_subdirectory(FM24CL04B)
    endif()
    if(FIRMWARE_BUILD_TESTS_MAX35103)
        add_subdirectory(MAX35103)
    endif()
    if(FIRMWARE_BUILD_TESTS_ZSSC3241)
        add_subdirectory(ZSSC3241)
    endif()
endif()
