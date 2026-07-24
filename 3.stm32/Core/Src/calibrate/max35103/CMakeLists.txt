# =============================================================================
# Calibrate: MAX35103 auto-calibration service
#
# Gộp portable autocal core (max35103_autocal.c) và STM32 board wrapper
# (autocal_board.c) vào cùng một static library riêng biệt để tách biệt khỏi
# production runtime — firmware chỉ trả code-size cost khi bật autocal.
# =============================================================================
add_library(calibrate_max35103 STATIC
    src/max35103_autocal.c
    src/autocal_board.c
)

add_library(swfpm::calibrate_max35103 ALIAS calibrate_max35103)

target_include_directories(calibrate_max35103
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_compile_features(calibrate_max35103
    PUBLIC
        c_std_11
)

target_link_libraries(calibrate_max35103
    PUBLIC
        driver_max35103_core
    PRIVATE
        stm32cubemx
)