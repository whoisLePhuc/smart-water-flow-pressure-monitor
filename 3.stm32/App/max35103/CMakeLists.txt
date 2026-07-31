# Application-level MAX35103 measurement state machine (AUTOCAL + MEASURING).
#
# Depends on the auto-calibration service (auto_calibrate_max35103), which links
# the portable MAX35103 core and propagates FIRMWARE_BUILD_MAX35103_AUTOCAL.
add_library(app_max35103 STATIC
    src/app_max35103.c
)

add_library(swfpm::app_max35103 ALIAS app_max35103)

target_include_directories(
    app_max35103
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_compile_features(
    app_max35103
    PUBLIC c_std_11
)

target_link_libraries(
    app_max35103
    PUBLIC
        driver_max35103_core
    PRIVATE
        auto_calibrate_max35103
        driver_max35103_stm32_hal
        stm32cubemx
)
