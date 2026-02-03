#ifndef KVSTORE_UTIL_SIGNAL_HANDLER_HPP
#define KVSTORE_UTIL_SIGNAL_HANDLER_HPP

#include <atomic>
#include <functional>

namespace kvstore::util {

class SignalHandler {
   public:
    static void install();
    static bool should_shutdown();
    static void wait_for_shutdown();
    static void request_shutdown();
    static void reset();
    static bool waiting();  // true if any thread is in wait_for_shutdown()

   private:
    static std::atomic<bool> shutdown_requested_;
    static std::atomic<int> waiting_count_;
};

}  // namespace kvstore::util

#endif