#ifndef KVSTORE_UTIL_CLOCK_HPP
#define KVSTORE_UTIL_CLOCK_HPP

#include <chrono>
#include <memory>

namespace kvstore::util {

using TimePoint = std::chrono::steady_clock::time_point;
using Duration = std::chrono::milliseconds;

class Clock {
public:
    virtual ~Clock() = default;
    [[nodiscard]] virtual TimePoint now() const = 0;
};

class SystemClock : public Clock {
public:
    [[nodiscard]] TimePoint now() const override {
        return std::chrono::steady_clock::now();
    }
};

class MockClock : public Clock {
public:
    [[nodiscard]] TimePoint now() const override {
        return current_;
    }
    
    void set(TimePoint time) {
        current_ = time;
    }

    void advance(Duration duration) {
        current_ += duration;
    }
private:
    TimePoint current_ = std::chrono::steady_clock::now();
};

}//namespace kvstore::util

#endif