add_executable(linux_sim main.c)
target_include_directories(linux_sim PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/platform/include)
target_link_libraries(linux_sim PRIVATE app infrastructure platform_linux fw_ports fw_facade_measurement fw_facade_storage fw_facade_connectivity fw_facade_power project_compiler_options)
target_add_sanitizers(linux_sim)
