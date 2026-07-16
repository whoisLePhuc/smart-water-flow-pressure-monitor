# Compiler warning and sanitizer configuration
# Usage: target_link_libraries(<target> PRIVATE project_compiler_options)

add_library(project_compiler_options INTERFACE)

target_compile_options(project_compiler_options
    INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        # -Wno-error=unused-variable: test code uses assert() which is compiled
        # out in Release builds, leaving test assertion variables unused.
        # The variables are still warned about (-Wunused-variable) but not
        # treated as errors, preventing Release build failures in test files.
        -Wno-error=unused-variable
        -Wno-error=unused-but-set-variable
        -Wstrict-prototypes
        -Wold-style-definition
        -Wmissing-prototypes
        -Wmissing-declarations
        -Wshadow
        -Wpointer-arith
        -Wcast-align
        -Wwrite-strings
        -Wconversion
        -Wsign-conversion
        -Wundef
        -Wunreachable-code
        -Winit-self
)

option(ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" ON)

function(target_add_sanitizers TARGET)
    if(ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        if(NOT CMAKE_CROSSCOMPILING)
            target_compile_options(${TARGET} PRIVATE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
            )
            target_link_options(${TARGET} PRIVATE
                -fsanitize=address,undefined
            )
        endif()
    endif()
endfunction()
