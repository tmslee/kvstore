add_executable(kvstore_test
    kvstore_test.cpp
)
target_link_libraries(kvstore_test
    PRIVATE
        kvstore
        GTest::gtest_main
)

add_executable(wal_test
    wal_test.cpp
)
target_link_libraries(wal_test
    PRIVATE
        kvstore
        GTest::gtest_main
)

add_executable(server_test
    server_test.cpp
)
target_link_libraries(server_test
    PRIVATE
        kvstore
        GTest::gtest_main
)

if(ENABLE_ASAN OR ENABLE_TSAN OR ENABLE_UBSAN)
    # Sanitizer builds: skip discovery, just run the executable
    add_test(NAME kvstore_test COMMAND kvstore_test)
    add_test(NAME wal_test COMMAND wal_test)
    add_test(NAME server_test COMMAND server_test)

else()
    # Normal builds: use discovery for better CTest integration
    include(GoogleTest)
    gtest_discover_tests(kvstore_test)
    gtest_discover_tests(wal_test)
    gtest_discover_tests(server_test)
endif()