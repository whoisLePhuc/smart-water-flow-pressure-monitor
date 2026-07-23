# =============================================================================
# Driver: portable Renesas ZSSC3241 core
# =============================================================================

add_library(driver_zssc3241_core STATIC
    zssc3241.c
)

add_library(driver_zssc3241 ALIAS driver_zssc3241_core)
add_library(swfpm::driver_zssc3241 ALIAS driver_zssc3241_core)

target_include_directories(driver_zssc3241_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(driver_zssc3241_core
    PUBLIC
        c_std_11
)

# Build the platform adapter only inside an STM32 CubeMX project. Host builds
# can compile and test driver_zssc3241_core without HAL headers or libraries.
if(TARGET stm32cubemx)
    add_library(driver_zssc3241_stm32_hal STATIC
        zssc3241_stm32_hal.c
    )

    add_library(swfpm::driver_zssc3241_stm32_hal
        ALIAS driver_zssc3241_stm32_hal)

    target_include_directories(driver_zssc3241_stm32_hal
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_features(driver_zssc3241_stm32_hal
        PUBLIC
            c_std_11
    )

    target_link_libraries(driver_zssc3241_stm32_hal
        PUBLIC
            driver_zssc3241_core
        PRIVATE
            stm32cubemx
    )
endif()

option(ZSSC3241_BUILD_TESTS "Build ZSSC3241 host tests" OFF)
if(ZSSC3241_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
