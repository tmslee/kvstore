#ifndef KVSTORE_UTIL_FD_GUARD_HPP
#define KVSTORE_UTIL_FD_GUARD_HPP

#include <unistd.h>

namespace kvstore::util {

// RAII wrapper for file descriptors - auto-closes on destruction
class FdGuard {
   public:
    explicit FdGuard(int fd = -1) : fd_(fd) {}

    ~FdGuard() noexcept {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    // Non-copyable
    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;

    // Movable
    FdGuard(FdGuard&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    FdGuard& operator=(FdGuard&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) {
                close(fd_);
            }
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept {
        return fd_;
    }

    // Release ownership - caller is responsible for closing
    int release() noexcept {
        int fd = fd_;
        fd_ = -1;
        return fd;
    }

    // Reset to a new fd (closes current if valid)
    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = fd;
    }

    [[nodiscard]] bool valid() const noexcept {
        return fd_ >= 0;
    }
    explicit operator bool() const noexcept {
        return valid();
    }

   private:
    int fd_ = -1;
};

}  // namespace kvstore::util

#endif  // KVSTORE_UTIL_FD_GUARD_HPP
