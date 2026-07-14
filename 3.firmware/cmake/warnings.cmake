# Compiler warning and sanitizer configuration
# Included by the top-level CMakeLists.txt

# Strict warning flags (C11)
set(CORE_WARNINGS
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

# Sanitizer flags — Debug only, excluded for cross-compile (STM32)
option(ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" ON)

macro(target_add_sanitizers TARGET)
    if(ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        # Check we're not cross-compiling
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
endmacro()
