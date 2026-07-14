add_executable(linux_sim
    main.c
)

target_link_libraries(linux_sim
    PRIVATE
        core
        platform_linux
        project_compiler_options
)

target_add_sanitizers(linux_sim)
