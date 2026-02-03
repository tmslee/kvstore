#include "kvstore/util/signal_handler.hpp"

#include <condition_variable>
#include <csignal>
#include <mutex>

namespace kvstore::util {

std::atomic<bool> SignalHandler::shutdown_requested_{false};
std::atomic<int> SignalHandler::waiting_count_{0};

namespace {

std::mutex shutdown_mutex;
std::condition_variable shutdown_cv;

void signal_handler(int signal) {
    (void)signal;
    SignalHandler::request_shutdown();
}

}  // namespace

void SignalHandler::install() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
}

bool SignalHandler::should_shutdown() {
    return shutdown_requested_.load();
}

void SignalHandler::wait_for_shutdown() {
    std::unique_lock lock(shutdown_mutex);
    ++waiting_count_;
    shutdown_cv.wait(lock, [] { return shutdown_requested_.load(); });
    --waiting_count_;
}

bool SignalHandler::waiting() {
    return waiting_count_.load() > 0;
}

void SignalHandler::request_shutdown() {
    shutdown_requested_.store(true);
    shutdown_cv.notify_all();
}

void SignalHandler::reset() {
    shutdown_requested_.store(false);
    waiting_count_.store(0);
}

}  // namespace kvstore::util