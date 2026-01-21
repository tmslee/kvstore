#ifndef KVSTORE_UTIL_TYPES_HPP
#define KVSTORE_UTIL_TYPES_HPP

#include <chrono>
#include <cstint>
#include <optional>

namespace kvstore::util {

using TimePoint = std::chrono::steady_clock::time_point;
using Duration = std::chrono::milliseconds;
using ExpirationTime = std::optional<int64_t>;

inline int64_t to_epoch_ms(TimePoint tp) {
    auto duration = tp.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

inline TimePoint from_epoch_ms(int64_t ms) {
    return TimePoint(std::chrono::milliseconds(ms));
}

} //namespace kvstore::util

#endif