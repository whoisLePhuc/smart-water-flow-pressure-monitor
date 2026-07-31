# MAX35103 auto-calibration service.
#
# The portable calibration core and STM32 board adapter are kept in a separate
# static library so production firmware only incurs their code-size cost when
# this target is linked.
add_library(auto_calibrate_max35103 STATIC
    src/max35103_autocal.c
    src/autocal_board.c
)

add_library(swfpm::auto_calibrate_max35103 ALIAS auto_calibrate_max35103)

target_include_directories(
    auto_calibrate_max35103
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_compile_features(
    auto_calibrate_max35103
    PUBLIC c_std_11
)

target_link_libraries(
    auto_calibrate_max35103
    PUBLIC
        driver_max35103_core
    PRIVATE
        stm32cubemx
)
