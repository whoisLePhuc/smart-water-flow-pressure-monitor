add_library(fw_infra_repository STATIC data_repository.c repo_transaction.c)
target_include_directories(fw_infra_repository PUBLIC
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fw_infra_repository PUBLIC core project_compiler_options)
target_add_sanitizers(fw_infra_repository)
