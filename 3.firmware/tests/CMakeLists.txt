function(add_core_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE core project_compiler_options)
    target_add_sanitizers(${TEST_NAME})
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# Core unit tests (pure core, no platform backend)
add_core_test(test_event_queue       test_event_queue.c)
add_core_test(test_data_repository   test_data_repository.c)
add_core_test(test_system_fsm        test_system_fsm.c)

# Scheduler test — needs platform_linux for monotonic_now_us
add_executable(test_scheduler test_scheduler.c)
target_link_libraries(test_scheduler PRIVATE core platform_linux project_compiler_options)
target_add_sanitizers(test_scheduler)
add_test(NAME test_scheduler COMMAND test_scheduler)

# Integration test — uses platform backend
add_executable(test_linux_simulation test_linux_simulation.c)
target_link_libraries(test_linux_simulation PRIVATE core platform_linux project_compiler_options)
target_add_sanitizers(test_linux_simulation)
add_test(NAME test_linux_simulation COMMAND test_linux_simulation)
