add_executable(kvstore_tests
    kvstore_test.cpp
)

target_link_libraries(kvstore_tests
    PRIVATE
        kvstore
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(kvstore_tests)