add_library(services STATIC measurement_manager.c processing_stubs.c calibration_service.c flow_service.c pressure_service.c)
target_include_directories(services PUBLIC ${CMAKE_SOURCE_DIR}/src PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(services PUBLIC app infrastructure drivers project_compiler_options)
target_add_sanitizers(services)