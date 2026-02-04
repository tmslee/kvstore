#include "kvstore/util/binary_io.hpp"

#include <fcntl.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace kvstore::util::test {

class BinaryIoTest : public ::testing::Test {
   protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "binary_io_test";
        std::filesystem::create_directories(test_dir_);
        test_file_ = test_dir_ / "test.bin";
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path test_file_;
};

// =============================================================================
// File descriptor I/O tests
// =============================================================================

TEST_F(BinaryIoTest, WriteAndReadIntFd) {
    // Write integers of various sizes
    {
        int fd = open(test_file_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(fd, 0);

        write_int_fd<uint8_t>(fd, 0x12);
        write_int_fd<uint16_t>(fd, 0x3456);
        write_int_fd<uint32_t>(fd, 0x789ABCDE);
        write_int_fd<uint64_t>(fd, 0x0102030405060708ULL);

        close(fd);
    }

    // Read them back
    {
        int fd = open(test_file_.c_str(), O_RDONLY);
        ASSERT_GE(fd, 0);

        uint8_t val8;
        uint16_t val16;
        uint32_t val32;
        uint64_t val64;

        EXPECT_TRUE(read_int_fd<uint8_t>(fd, val8));
        EXPECT_TRUE(read_int_fd<uint16_t>(fd, val16));
        EXPECT_TRUE(read_int_fd<uint32_t>(fd, val32));
        EXPECT_TRUE(read_int_fd<uint64_t>(fd, val64));

        EXPECT_EQ(val8, 0x12);
        EXPECT_EQ(val16, 0x3456);
        EXPECT_EQ(val32, 0x789ABCDE);
        EXPECT_EQ(val64, 0x0102030405060708ULL);

        close(fd);
    }
}

TEST_F(BinaryIoTest, WriteAndReadStringFd) {
    std::string test_str = "Hello, World!";
    std::string empty_str = "";
    std::string binary_str = std::string("\x00\x01\x02\x03", 4);

    // Write strings
    {
        int fd = open(test_file_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(fd, 0);

        write_string_fd(fd, test_str);
        write_string_fd(fd, empty_str);
        write_string_fd(fd, binary_str);

        close(fd);
    }

    // Read them back
    {
        int fd = open(test_file_.c_str(), O_RDONLY);
        ASSERT_GE(fd, 0);

        std::string read1, read2, read3;

        EXPECT_TRUE(read_string_fd(fd, read1));
        EXPECT_TRUE(read_string_fd(fd, read2));
        EXPECT_TRUE(read_string_fd(fd, read3));

        EXPECT_EQ(read1, test_str);
        EXPECT_EQ(read2, empty_str);
        EXPECT_EQ(read3, binary_str);

        close(fd);
    }
}

TEST_F(BinaryIoTest, ReadIntFdFailsAtEof) {
    // Create empty file
    {
        int fd = open(test_file_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(fd, 0);
        close(fd);
    }

    // Try to read from empty file
    {
        int fd = open(test_file_.c_str(), O_RDONLY);
        ASSERT_GE(fd, 0);

        uint32_t val;
        EXPECT_FALSE(read_int_fd<uint32_t>(fd, val));

        close(fd);
    }
}

TEST_F(BinaryIoTest, ReadStringFdFailsAtEof) {
    // Create file with only length prefix (no data)
    {
        int fd = open(test_file_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(fd, 0);
        write_int_fd<uint32_t>(fd, 100);  // Length says 100 bytes, but no data follows
        close(fd);
    }

    // Try to read - should fail
    {
        int fd = open(test_file_.c_str(), O_RDONLY);
        ASSERT_GE(fd, 0);

        std::string str;
        EXPECT_FALSE(read_string_fd(fd, str));

        close(fd);
    }
}

TEST_F(BinaryIoTest, ReadStringFdRejectsOversized) {
    // Create file with huge length prefix
    {
        int fd = open(test_file_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(fd, 0);
        write_int_fd<uint32_t>(fd, kMaxStringSize + 1);
        close(fd);
    }

    // Try to read - should fail due to size limit
    {
        int fd = open(test_file_.c_str(), O_RDONLY);
        ASSERT_GE(fd, 0);

        std::string str;
        EXPECT_FALSE(read_string_fd(fd, str));

        close(fd);
    }
}

TEST_F(BinaryIoTest, ReadStringFdCustomMaxSize) {
    std::string test_str = "This is a test string";

    // Write string
    {
        int fd = open(test_file_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(fd, 0);
        write_string_fd(fd, test_str);
        close(fd);
    }

    // Read with max_size smaller than string - should fail
    {
        int fd = open(test_file_.c_str(), O_RDONLY);
        ASSERT_GE(fd, 0);

        std::string str;
        EXPECT_FALSE(read_string_fd(fd, str, 10));  // max_size = 10, string is longer

        close(fd);
    }
}

// =============================================================================
// Buffer-based I/O tests (network protocol)
// =============================================================================

TEST_F(BinaryIoTest, WriteAndReadIntBuffer) {
    std::vector<uint8_t> buf;

    write_int<uint8_t>(buf, 0x12);
    write_int<uint16_t>(buf, 0x3456);
    write_int<uint32_t>(buf, 0x789ABCDE);
    write_int<uint64_t>(buf, 0x0102030405060708ULL);

    // Should be big-endian
    EXPECT_EQ(buf.size(), 1 + 2 + 4 + 8);
    EXPECT_EQ(buf[0], 0x12);
    EXPECT_EQ(buf[1], 0x34);
    EXPECT_EQ(buf[2], 0x56);

    // Read back
    size_t offset = 0;
    EXPECT_EQ(read_int<uint8_t>(buf.data(), offset, buf.size()), 0x12);
    EXPECT_EQ(read_int<uint16_t>(buf.data(), offset, buf.size()), 0x3456);
    EXPECT_EQ(read_int<uint32_t>(buf.data(), offset, buf.size()), 0x789ABCDE);
    EXPECT_EQ(read_int<uint64_t>(buf.data(), offset, buf.size()), 0x0102030405060708ULL);
    EXPECT_EQ(offset, buf.size());
}

TEST_F(BinaryIoTest, WriteAndReadStringBuffer) {
    std::vector<uint8_t> buf;
    std::string test_str = "Hello";

    write_string(buf, test_str);

    // Length prefix (4 bytes big-endian) + string data
    EXPECT_EQ(buf.size(), 4 + 5);
    EXPECT_EQ(buf[0], 0x00);
    EXPECT_EQ(buf[1], 0x00);
    EXPECT_EQ(buf[2], 0x00);
    EXPECT_EQ(buf[3], 0x05);

    size_t offset = 0;
    std::string result = read_string(buf.data(), offset, buf.size());
    EXPECT_EQ(result, test_str);
    EXPECT_EQ(offset, buf.size());
}

TEST_F(BinaryIoTest, ReadIntBufferThrowsOnUnderflow) {
    std::vector<uint8_t> buf = {0x01, 0x02};  // Only 2 bytes

    size_t offset = 0;
    EXPECT_THROW(read_int<uint32_t>(buf.data(), offset, buf.size()), std::runtime_error);
}

TEST_F(BinaryIoTest, ReadIntBufferThrowsOnOffsetOverflow) {
    std::vector<uint8_t> buf = {0x01, 0x02, 0x03, 0x04};

    size_t offset = 3;  // Only 1 byte left
    EXPECT_THROW(read_int<uint32_t>(buf.data(), offset, buf.size()), std::runtime_error);
}

TEST_F(BinaryIoTest, ReadStringBufferThrowsOnUnderflow) {
    std::vector<uint8_t> buf;
    write_int<uint32_t>(buf, 100);  // Length says 100 bytes

    size_t offset = 0;
    EXPECT_THROW(read_string(buf.data(), offset, buf.size()), std::runtime_error);
}

TEST_F(BinaryIoTest, ReadIntNoOffsetVersion) {
    std::vector<uint8_t> buf;
    write_int<uint32_t>(buf, 0x12345678);

    // Use the no-offset version (reads from start)
    EXPECT_EQ(read_int<uint32_t>(buf.data()), 0x12345678);
}

TEST_F(BinaryIoTest, EmptyStringRoundtrip) {
    std::vector<uint8_t> buf;
    write_string(buf, "");

    size_t offset = 0;
    std::string result = read_string(buf.data(), offset, buf.size());
    EXPECT_EQ(result, "");
}

TEST_F(BinaryIoTest, BinaryDataInString) {
    std::vector<uint8_t> buf;
    std::string binary_data = std::string("\x00\xFF\x01\xFE", 4);

    write_string(buf, binary_data);

    size_t offset = 0;
    std::string result = read_string(buf.data(), offset, buf.size());
    EXPECT_EQ(result, binary_data);
    EXPECT_EQ(result.size(), 4);
}

}  // namespace kvstore::util::test
