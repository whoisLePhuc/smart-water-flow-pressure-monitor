# =============================================================================
# Driver: portable MAX35103 core
# =============================================================================
add_library(driver_max35103_core STATIC
    max35103.c
)

add_library(driver_max35103 ALIAS driver_max35103_core)
add_library(swfpm::driver_max35103 ALIAS driver_max35103_core)

target_include_directories(driver_max35103_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(driver_max35103_core
    PUBLIC
        c_std_11
)

# Characterization and manufacturing service. Keeping it as a separate target
# prevents the production runtime from paying its code-size cost unless the
# application explicitly enables auto-calibration.
add_library(service_max35103_autocal STATIC
    max35103_autocal.c
)

add_library(swfpm::service_max35103_autocal
    ALIAS service_max35103_autocal)

target_include_directories(service_max35103_autocal
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(service_max35103_autocal
    PUBLIC
        c_std_11
)

target_link_libraries(service_max35103_autocal
    PUBLIC
        driver_max35103_core
)

# Build the platform adapter only inside an STM32 CubeMX project. Host builds
# can compile and test driver_max35103_core without HAL headers or libraries.
if(TARGET stm32cubemx)
    add_library(driver_max35103_stm32_hal STATIC
        max35103_stm32_hal.c
    )

    add_library(swfpm::driver_max35103_stm32_hal
        ALIAS driver_max35103_stm32_hal)

    target_include_directories(driver_max35103_stm32_hal
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_features(driver_max35103_stm32_hal
        PUBLIC
            c_std_11
    )

    target_link_libraries(driver_max35103_stm32_hal
        PUBLIC
            driver_max35103_core
        PRIVATE
            stm32cubemx
    )

endif()
