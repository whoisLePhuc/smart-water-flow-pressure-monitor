# =============================================================================
# Driver: FM24CL04B - FRAM (Ferroelectric RAM)
# Interface: I2C
# Dependency: stm32cubemx (HAL I2C)
# =============================================================================
add_library(driver_fram STATIC
    fram_driver.c
)

target_include_directories(driver_fram
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(driver_fram
    PRIVATE
        stm32cubemx
)
