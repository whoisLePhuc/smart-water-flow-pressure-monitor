add_library(fw_infra_numeric STATIC checked_math.c interpolation.c)
target_include_directories(fw_infra_numeric PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fw_infra_numeric PUBLIC core project_compiler_options)
target_add_sanitizers(fw_infra_numeric)
