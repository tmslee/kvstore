#ifndef KVSTORE_UTIL_SIGNAL_HANDLER_HPP
#define KVSTORE_UTIL_SIGNAL_HANDLER_HPP

#include <atomic>
#include <functional>

namespace kvstore::util{

class SignalHandler{
public:
    static void install();
    static bool should_shutdown();
    static void wait_for_shutdown();
    static void request_shutdown();

private:
    static std::atomic<bool>shutdown_requested_;
};

} //namespace kvstore::util

#endif