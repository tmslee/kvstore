add_executable(store_test
    core/store_test.cpp
)
target_link_libraries(store_test
    PRIVATE
        kvstore
        GTest::gtest_main
)

add_executable(wal_test
    core/wal_test.cpp
)
target_link_libraries(wal_test
    PRIVATE
        kvstore
        GTest::gtest_main
)

add_executable(server_test
    net/server_test.cpp
)
target_link_libraries(server_test
    PRIVATE
        kvstore
        GTest::gtest_main
)

add_executable(protocol_test
    net/protocol_test.cpp
)
target_link_libraries(protocol_test
    PRIVATE
        kvstore
        GTest::gtest_main
)

add_executable(client_test
    net/client_test.cpp
)
target_link_libraries(client_test
    PRIVATE
        kvstore
        GTest::gtest_main
)

add_executable(snapshot_test
    core/snapshot_test.cpp
)
target_link_libraries(snapshot_test
    PRIVATE
        kvstore
        GTest::gtest_main
)

if(ENABLE_ASAN OR ENABLE_TSAN OR ENABLE_UBSAN)
    # Sanitizer builds: skip discovery, just run the executable
    add_test(NAME store_test COMMAND store_test)
    add_test(NAME wal_test COMMAND wal_test)
    add_test(NAME server_test COMMAND server_test)
    add_test(NAME protocol_test COMMAND protocol_test)
    add_test(NAME client_test COMMAND client_test)
    add_test(NAME snapshot_test COMMAND snapshot_test)

else()
    # Normal builds: use discovery for better CTest integration
    include(GoogleTest)
    gtest_discover_tests(store_test)
    gtest_discover_tests(wal_test)
    gtest_discover_tests(server_test)
    gtest_discover_tests(protocol_test)
    gtest_discover_tests(client_test)
    gtest_discover_tests(snapshot_test)

endif()