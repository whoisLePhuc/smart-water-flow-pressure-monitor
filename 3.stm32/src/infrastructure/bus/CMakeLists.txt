add_library(fw_infra_bus STATIC i2c_bus_manager.c)
target_include_directories(fw_infra_bus PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fw_infra_bus PUBLIC fw_infra_event core project_compiler_options)