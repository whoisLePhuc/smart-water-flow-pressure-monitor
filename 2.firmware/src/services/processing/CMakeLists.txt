add_library(fw_service_processing STATIC flow_service.c pressure_service.c)
target_include_directories(fw_service_processing PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fw_service_processing PUBLIC
    fw_service_configuration fw_infra_event fw_infra_repository
    fw_infra_numeric project_compiler_options)
target_add_sanitizers(fw_service_processing)
