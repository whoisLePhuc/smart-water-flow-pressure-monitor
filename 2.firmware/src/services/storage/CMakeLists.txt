add_library(fw_service_storage STATIC storage_service.c checkpoint_policy.c)
target_include_directories(fw_service_storage PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fw_service_storage PUBLIC
    fw_protocol_storage fw_driver_storage fw_service_volume
    fw_infra_repository project_compiler_options)
target_add_sanitizers(fw_service_storage)
