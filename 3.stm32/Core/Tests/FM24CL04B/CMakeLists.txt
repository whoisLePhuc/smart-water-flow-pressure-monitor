# =============================================================================
# Test: FM24CL04B FRAM driver
# =============================================================================
add_library(test_fram STATIC
    fram_test.c
)

target_include_directories(test_fram
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(test_fram
    PUBLIC
        c_std_11
)

target_link_libraries(test_fram
    PRIVATE
        driver_fram
        stm32cubemx
)
