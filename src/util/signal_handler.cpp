#include "kvstore/util/signal_handler.hpp"

#include <csignal>
#include <condition_variable>
#include <mutex>

namespace kvstore::util {

std::atomic<bool> SignalHandler::shutown_requested_{false};

namespace {

std::mutex shutdown_mutex;
std::condition_variable shutdown_cv;

void signal_handler(int signal) {
    (void)signal;
    SignalHandler::request_shutdown();
}

} //namespace

void SignalHandler::install {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
}

bool SignalHandler::should_shutdown() {
    return shutdown_requested_.load();
}

void SignalHandler::wait_for_shutdown() {
    std::unique_lock lock(shutdown_mutex);
    shutdown_cv.wait(lock, []{return shutdown_requested_.load();});
}

void SignalHandler::request_shutdown() {
    shutdown_requested_.store(true);
    shutdown_cv.notify_all();
}

void SignalHandler::reset() {
    shutdown_requested_.store(false);
}

} //namespace kvstore::util