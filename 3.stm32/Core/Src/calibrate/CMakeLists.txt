# =============================================================================
# Calibration: MAX35103 AutoCal board integration
# =============================================================================
add_library(calibrate STATIC
    max35103_calibrate.c
)

target_include_directories(calibrate
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(calibrate
    PUBLIC
        c_std_11
)

target_link_libraries(calibrate
    PUBLIC
        service_max35103_autocal
        driver_max35103_stm32_hal
    PRIVATE
        stm32cubemx
)
