# Compiler warning and sanitizer configuration
# Usage: target_link_libraries(<target> PRIVATE project_compiler_options)

add_library(project_compiler_options INTERFACE)

target_compile_options(project_compiler_options
    INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
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
