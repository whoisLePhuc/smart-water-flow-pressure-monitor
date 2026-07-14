# ── Test configuration ────────────────────────────────────────
# Each test is a standalone executable linking core + fake clock

# Helper: create a test executable
function(add_core_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE core)
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )
    target_compile_options(${TEST_NAME} PRIVATE ${CORE_WARNINGS})
    target_add_sanitizers(${TEST_NAME})
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# ── Individual tests ─────────────────────────────────────────

add_core_test(test_event_queue    test_event_queue.c)
add_executable(test_scheduler test_scheduler.c)
target_link_libraries(test_scheduler PRIVATE core platform-linux)
target_include_directories(test_scheduler PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/linux
)
target_compile_options(test_scheduler PRIVATE ${CORE_WARNINGS})
target_add_sanitizers(test_scheduler)
add_test(NAME test_scheduler COMMAND test_scheduler)
add_core_test(test_data_repository test_data_repository.c)
add_core_test(test_system_fsm     test_system_fsm.c)

# Integration test — uses Linux platform backend
add_executable(test_linux_simulation test_linux_simulation.c)
target_link_libraries(test_linux_simulation PRIVATE core platform-linux)
target_include_directories(test_linux_simulation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/linux
)
target_compile_options(test_linux_simulation PRIVATE ${CORE_WARNINGS})
target_add_sanitizers(test_linux_simulation)
add_test(NAME test_linux_simulation COMMAND test_linux_simulation)
