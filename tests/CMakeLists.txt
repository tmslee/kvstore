# =============================================================================
# Unit Tests - test individual components in isolation
# =============================================================================

# Core unit tests
add_executable(store_test core/store_test.cpp)
target_link_libraries(store_test PRIVATE kvstore GTest::gtest_main)

add_executable(wal_test core/wal_test.cpp)
target_link_libraries(wal_test PRIVATE kvstore GTest::gtest_main)

add_executable(snapshot_test core/snapshot_test.cpp)
target_link_libraries(snapshot_test PRIVATE kvstore GTest::gtest_main)

add_executable(ttl_test core/ttl_test.cpp)
target_link_libraries(ttl_test PRIVATE kvstore GTest::gtest_main)

# Protocol unit tests
add_executable(text_protocol_test net/text_protocol_test.cpp)
target_link_libraries(text_protocol_test PRIVATE kvstore GTest::gtest_main)

add_executable(binary_protocol_test net/binary_protocol_test.cpp)
target_link_libraries(binary_protocol_test PRIVATE kvstore GTest::gtest_main)

# Util unit tests
add_executable(logger_test util/logger_test.cpp)
target_link_libraries(logger_test PRIVATE kvstore GTest::gtest_main)

add_executable(config_test util/config_test.cpp)
target_link_libraries(config_test PRIVATE kvstore GTest::gtest_main)

add_executable(binary_io_test util/binary_io_test.cpp)
target_link_libraries(binary_io_test PRIVATE kvstore GTest::gtest_main)

add_executable(fd_guard_test util/fd_guard_test.cpp)
target_link_libraries(fd_guard_test PRIVATE kvstore GTest::gtest_main)

# Server component unit tests
add_executable(connection_test net/server/connection_test.cpp)
target_link_libraries(connection_test PRIVATE kvstore GTest::gtest_main)

add_executable(event_loop_test net/server/event_loop_test.cpp)
target_link_libraries(event_loop_test PRIVATE kvstore GTest::gtest_main)

# =============================================================================
# Integration Tests - test multiple components working together
# =============================================================================

add_executable(server_test net/server/server_test.cpp)
target_link_libraries(server_test PRIVATE kvstore GTest::gtest_main)

add_executable(client_test net/client/client_test.cpp)
target_link_libraries(client_test PRIVATE kvstore GTest::gtest_main)

add_executable(disk_store_test core/disk_store_test.cpp)
target_link_libraries(disk_store_test PRIVATE kvstore GTest::gtest_main)

add_executable(concurrent_test core/concurrent_test.cpp)
target_link_libraries(concurrent_test PRIVATE kvstore GTest::gtest_main)

add_executable(signal_handler_test util/signal_handler_test.cpp)
target_link_libraries(signal_handler_test PRIVATE kvstore GTest::gtest_main)

add_executable(event_loop_integration_test net/server/event_loop_integration_test.cpp)
target_link_libraries(event_loop_integration_test PRIVATE kvstore GTest::gtest_main)

# =============================================================================
# Test Discovery and Labels
# =============================================================================

# Lists for labeling
set(UNIT_TESTS
    store_test
    wal_test
    snapshot_test
    ttl_test
    text_protocol_test
    binary_protocol_test
    logger_test
    config_test
    binary_io_test
    fd_guard_test
    connection_test
    event_loop_test
)

set(INTEGRATION_TESTS
    server_test
    client_test
    disk_store_test
    concurrent_test
    signal_handler_test
    event_loop_integration_test
)

if(ENABLE_ASAN OR ENABLE_TSAN OR ENABLE_UBSAN)
    # Sanitizer builds: skip discovery, just run the executable
    foreach(test ${UNIT_TESTS})
        add_test(NAME ${test} COMMAND ${test})
        set_tests_properties(${test} PROPERTIES LABELS "unit")
    endforeach()

    foreach(test ${INTEGRATION_TESTS})
        add_test(NAME ${test} COMMAND ${test})
        set_tests_properties(${test} PROPERTIES LABELS "integration")
    endforeach()
else()
    # Normal builds: use discovery for better CTest integration
    include(GoogleTest)

    foreach(test ${UNIT_TESTS})
        gtest_discover_tests(${test} PROPERTIES LABELS "unit")
    endforeach()

    foreach(test ${INTEGRATION_TESTS})
        gtest_discover_tests(${test} PROPERTIES LABELS "integration")
    endforeach()
endif()
