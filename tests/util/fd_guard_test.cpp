#include "kvstore/util/fd_guard.hpp"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>

namespace kvstore::util::test {

class FdGuardTest : public ::testing::Test {
   protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "fd_guard_test";
        std::filesystem::create_directories(test_dir_);
        test_file_ = test_dir_ / "test.txt";
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    // Helper to check if fd is valid
    bool is_fd_valid(int fd) {
        return fcntl(fd, F_GETFD) != -1;
    }

    std::filesystem::path test_dir_;
    std::filesystem::path test_file_;
};

TEST_F(FdGuardTest, DefaultConstructor) {
    FdGuard guard;
    EXPECT_EQ(guard.get(), -1);
    EXPECT_FALSE(guard.valid());
    EXPECT_FALSE(static_cast<bool>(guard));
}

TEST_F(FdGuardTest, ConstructWithValidFd) {
    int fd = open(test_file_.c_str(), O_WRONLY | O_CREAT, 0644);
    ASSERT_GE(fd, 0);

    FdGuard guard(fd);
    EXPECT_EQ(guard.get(), fd);
    EXPECT_TRUE(guard.valid());
    EXPECT_TRUE(static_cast<bool>(guard));
    EXPECT_TRUE(is_fd_valid(fd));
}

TEST_F(FdGuardTest, DestructorClosesFd) {
    int fd = open(test_file_.c_str(), O_WRONLY | O_CREAT, 0644);
    ASSERT_GE(fd, 0);
    ASSERT_TRUE(is_fd_valid(fd));

    {
        FdGuard guard(fd);
        EXPECT_TRUE(is_fd_valid(fd));
    }
    // After guard goes out of scope, fd should be closed
    EXPECT_FALSE(is_fd_valid(fd));
}

TEST_F(FdGuardTest, MoveConstructor) {
    int fd = open(test_file_.c_str(), O_WRONLY | O_CREAT, 0644);
    ASSERT_GE(fd, 0);

    FdGuard guard1(fd);
    FdGuard guard2(std::move(guard1));

    // guard1 should be invalidated
    EXPECT_EQ(guard1.get(), -1);
    EXPECT_FALSE(guard1.valid());

    // guard2 should own the fd
    EXPECT_EQ(guard2.get(), fd);
    EXPECT_TRUE(guard2.valid());
    EXPECT_TRUE(is_fd_valid(fd));
}

TEST_F(FdGuardTest, MoveAssignment) {
    int fd1 = open(test_file_.c_str(), O_WRONLY | O_CREAT, 0644);
    ASSERT_GE(fd1, 0);

    std::filesystem::path test_file2 = test_dir_ / "test2.txt";
    int fd2 = open(test_file2.c_str(), O_WRONLY | O_CREAT, 0644);
    ASSERT_GE(fd2, 0);

    FdGuard guard1(fd1);
    FdGuard guard2(fd2);

    // Move assign - should close fd2 and transfer fd1
    guard2 = std::move(guard1);

    EXPECT_EQ(guard1.get(), -1);
    EXPECT_EQ(guard2.get(), fd1);
    EXPECT_TRUE(is_fd_valid(fd1));
    EXPECT_FALSE(is_fd_valid(fd2));  // fd2 should have been closed
}

TEST_F(FdGuardTest, SelfMoveAssignment) {
    int fd = open(test_file_.c_str(), O_WRONLY | O_CREAT, 0644);
    ASSERT_GE(fd, 0);

    FdGuard guard(fd);
    FdGuard* ptr = &guard;

    // Self-assignment should be safe
    guard = std::move(*ptr);

    EXPECT_EQ(guard.get(), fd);
    EXPECT_TRUE(is_fd_valid(fd));
}

TEST_F(FdGuardTest, Release) {
    int fd = open(test_file_.c_str(), O_WRONLY | O_CREAT, 0644);
    ASSERT_GE(fd, 0);

    FdGuard guard(fd);
    int released_fd = guard.release();

    EXPECT_EQ(released_fd, fd);
    EXPECT_EQ(guard.get(), -1);
    EXPECT_FALSE(guard.valid());
    EXPECT_TRUE(is_fd_valid(fd));  // fd should still be valid

    // Must close manually after release
    close(fd);
}

TEST_F(FdGuardTest, Reset) {
    int fd1 = open(test_file_.c_str(), O_WRONLY | O_CREAT, 0644);
    ASSERT_GE(fd1, 0);

    FdGuard guard(fd1);

    std::filesystem::path test_file2 = test_dir_ / "test2.txt";
    int fd2 = open(test_file2.c_str(), O_WRONLY | O_CREAT, 0644);
    ASSERT_GE(fd2, 0);

    // Reset to new fd - should close old one
    guard.reset(fd2);

    EXPECT_EQ(guard.get(), fd2);
    EXPECT_FALSE(is_fd_valid(fd1));  // fd1 should have been closed
    EXPECT_TRUE(is_fd_valid(fd2));
}

TEST_F(FdGuardTest, ResetToInvalid) {
    int fd = open(test_file_.c_str(), O_WRONLY | O_CREAT, 0644);
    ASSERT_GE(fd, 0);

    FdGuard guard(fd);
    guard.reset();  // Reset to -1

    EXPECT_EQ(guard.get(), -1);
    EXPECT_FALSE(guard.valid());
    EXPECT_FALSE(is_fd_valid(fd));  // fd should have been closed
}

TEST_F(FdGuardTest, ResetFromInvalid) {
    FdGuard guard;  // Invalid by default

    int fd = open(test_file_.c_str(), O_WRONLY | O_CREAT, 0644);
    ASSERT_GE(fd, 0);

    guard.reset(fd);

    EXPECT_EQ(guard.get(), fd);
    EXPECT_TRUE(guard.valid());
}

TEST_F(FdGuardTest, MultipleMoves) {
    int fd = open(test_file_.c_str(), O_WRONLY | O_CREAT, 0644);
    ASSERT_GE(fd, 0);

    FdGuard guard1(fd);
    FdGuard guard2(std::move(guard1));
    FdGuard guard3(std::move(guard2));

    EXPECT_FALSE(guard1.valid());
    EXPECT_FALSE(guard2.valid());
    EXPECT_TRUE(guard3.valid());
    EXPECT_EQ(guard3.get(), fd);
    EXPECT_TRUE(is_fd_valid(fd));
}

}  // namespace kvstore::util::test
