add_executable(linux_sim main.c)
target_include_directories(linux_sim PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/platform/include ${CMAKE_SOURCE_DIR}/src/platform/linux/adapters)
target_link_libraries(linux_sim PRIVATE fw_infra_runtime fw_infra_bus fw_infra_numeric platform_linux fw_adapter_linux fw_service_connectivity fw_ports fw_facade_measurement fw_facade_storage fw_facade_connectivity fw_facade_power app project_compiler_options)
target_add_sanitizers(linux_sim)
