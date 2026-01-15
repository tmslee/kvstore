add_executable(kvstore_tests
    kvstore_test.cpp
)

target_link_libraries(kvstore_tests
    PRIVATE
        kvstore
)

add_test(NAME kvstore_tests COMMAND kvstore_tests)