#include "kvstore/util/signal_handler.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <csignal>

namespace kvstore::util {

std::atomic<bool> SignalHandler::shutdown_requested_{false};
std::atomic<int> SignalHandler::waiting_count_{0};

namespace {

// Self-pipe for async-signal-safe notification
// Signal handlers can only safely call async-signal-safe functions.
// write() is async-signal-safe, but condition_variable::notify_all() is NOT.
int signal_pipe[2] = {-1, -1};

void signal_handler(int signal) {
    (void)signal;
    SignalHandler::set_shutdown_flag();
    // write() is async-signal-safe - just write a byte to wake up waiters
    char c = 1;
    [[maybe_unused]] auto ignored = write(signal_pipe[1], &c, 1);
}

void create_pipe_if_needed() {
    if (signal_pipe[0] >= 0) {
        return;  // already created
    }
    if (pipe(signal_pipe) < 0) {
        return;  // failed, wait_for_shutdown will spin
    }
    // make write end non-blocking so signal handler never blocks
    fcntl(signal_pipe[1], F_SETFL, O_NONBLOCK);
}

}  // namespace

void SignalHandler::install() {
    create_pipe_if_needed();
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
}

bool SignalHandler::should_shutdown() {
    return shutdown_requested_.load();
}

void SignalHandler::wait_for_shutdown() {
    create_pipe_if_needed();
    ++waiting_count_;

    // read blocks until signal handler writes a byte
    char c;
    while (!shutdown_requested_.load()) {
        [[maybe_unused]] auto ignored = read(signal_pipe[0], &c, 1);
    }

    --waiting_count_;
}

bool SignalHandler::waiting() {
    return waiting_count_.load() > 0;
}

void SignalHandler::request_shutdown() {
    shutdown_requested_.store(true);
    // wake up any waiters by writing to pipe
    char c = 1;
    [[maybe_unused]] auto ignored = write(signal_pipe[1], &c, 1);
}

void SignalHandler::reset() {
    shutdown_requested_.store(false);
    waiting_count_.store(0);
    // drain any pending bytes from the pipe
    if (signal_pipe[0] >= 0) {
        fcntl(signal_pipe[0], F_SETFL, O_NONBLOCK);
        char buf[16];
        while (read(signal_pipe[0], buf, sizeof(buf)) > 0) {
        }
        fcntl(signal_pipe[0], F_SETFL, 0);  // back to blocking
    }
}

}  // namespace kvstore::util
