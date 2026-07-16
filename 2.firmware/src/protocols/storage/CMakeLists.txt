add_library(fw_protocol_storage STATIC storage_codec.c)
target_include_directories(fw_protocol_storage
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(fw_protocol_storage
    INTERFACE
        project_compiler_options
)
target_add_sanitizers(fw_protocol_storage)
