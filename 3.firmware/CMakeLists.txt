cmake_minimum_required(VERSION 3.20)
project(swfpm-firmware
    VERSION 0.1.0
    DESCRIPTION "Smart Water Flow and Pressure Monitor - Firmware Core"
    LANGUAGES C
)

# C11 standard required
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Include warning configuration
include(cmake/warnings.cmake)

# Source groups for IDE navigation
set(CORE_SRC
    src/core/app_event.c
    src/core/app_event_queue.c
    src/core/app_event_loop.c
    src/core/monotonic_scheduler.c
    src/core/system_fsm.c
    src/core/mode_guard.c
    src/core/data_repository.c
)

set(CORE_PUBLIC_HDR
    include/core/app_event.h
    include/core/app_event_queue.h
    include/core/app_event_loop.h
    include/core/scheduler.h
    include/core/system_fsm.h
    include/core/mode_guard.h
    include/core/data_model.h
    include/core/data_repository.h
)

set(PLATFORM_PORT_HDR
    include/platform/monotonic_clock_port.h
    include/platform/system_control_port.h
    include/platform/platform_runtime.h
)

# ── Core static library ─────────────────────────────────────────
# Builds the portable firmware core. Zero platform dependency.
add_library(core STATIC
    ${CORE_SRC}
    ${CORE_PUBLIC_HDR}
    ${PLATFORM_PORT_HDR}
)

target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(core PRIVATE ${CORE_WARNINGS})
target_add_sanitizers(core)

# ── Linux platform backend ──────────────────────────────────────
set(LINUX_PLATFORM_SRC
    src/platform/linux/virtual_clock.c
    src/platform/linux/linux_system_control.c
    src/platform/linux/linux_platform_runtime.c
)

add_library(platform-linux STATIC
    ${LINUX_PLATFORM_SRC}
)

target_include_directories(platform-linux PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(platform-linux PRIVATE ${CORE_WARNINGS})
target_add_sanitizers(platform-linux)

# ── Linux simulator application ─────────────────────────────────
add_executable(linux_sim
    apps/linux_sim/main.c
)

target_link_libraries(linux_sim PRIVATE
    core
    platform-linux
)

target_compile_options(linux_sim PRIVATE ${CORE_WARNINGS})
target_add_sanitizers(linux_sim)

# ── Tests ───────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
