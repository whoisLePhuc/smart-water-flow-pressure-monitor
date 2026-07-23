# =============================================================================
# Test: ZSSC3241 sensor signal conditioner driver
# =============================================================================
add_library(test_zssc3241 STATIC
    zssc3241_test.c
)

add_library(swfpm::test_zssc3241 ALIAS test_zssc3241)

target_include_directories(test_zssc3241
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(test_zssc3241
    PUBLIC
        c_std_11
)

target_link_libraries(test_zssc3241
    PUBLIC
        driver_zssc3241_stm32_hal
    PRIVATE
        stm32cubemx
)