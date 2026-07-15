add_library(platform_linux STATIC
    virtual_clock.c
    linux_system_control.c
    linux_platform_runtime.c
    linux_scheduled_action_queue.c
    linux_run_controller.c
    providers/linux_spi_provider.c
    providers/linux_i2c_provider.c
    providers/linux_gpio_provider.c
    peers/peer_max35103.c
    peers/peer_zssc3241.c
    peers/peer_fram.c
)

target_include_directories(platform_linux
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(platform_linux
    PUBLIC
        core
        project_compiler_options
)

target_add_sanitizers(platform_linux)
