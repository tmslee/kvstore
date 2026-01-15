add_executable(kvstore_tests
    kvstore_test.cpp
)

target_link_libraries(kvstore_tests
    PRIVATE
        kvstore
        GTest::gtest_main
)


if(ENABLE_ASAN OR ENABLE_TSAN OR ENABLE_UBSAN)
    # Sanitizer builds: skip discovery, just run the executable
    add_test(NAME kvstore_tests COMMAND kvstore_tests)
else()
    # Normal builds: use discovery for better CTest integration
    include(GoogleTest)
    gtest_discover_tests(kvstore_tests)
endif()