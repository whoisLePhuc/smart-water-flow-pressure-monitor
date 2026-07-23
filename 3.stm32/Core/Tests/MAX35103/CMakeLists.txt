# =============================================================================
# Test: MAX35103 time-of-flight and temperature driver
# =============================================================================
if(TARGET stm32cubemx)
    add_library(test_max35103 STATIC
        max35103_test.c
    )

    target_include_directories(test_max35103
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_features(test_max35103
        PUBLIC
            c_std_11
    )

    target_link_libraries(test_max35103
        PUBLIC
            driver_max35103_core
        PRIVATE
            stm32cubemx
    )
endif()
