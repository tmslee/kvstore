#include "kvstore/net/server/event_loop.hpp"

#include <unistd.h>

#include <stdexcept>

#include "kvstore/util/logger.hpp"

namespace kvstore::net::server {

EventLoop::EventLoop() {
    // EPOLL_CLOEXEC: close epoll fd on exec() - prevents leaking fd to child processes
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        throw std::runtime_error("failed to create epoll instance");
    }
}

EventLoop::~EventLoop() {
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
    }
}

void EventLoop::add(int fd, uint32_t events, Callback callback) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        throw std::runtime_error("epoll_ctl ADD failed");
    }

    callbacks_[fd] = std::move(callback);
}

void EventLoop::modify(int fd, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
        throw std::runtime_error("epoll_ctl MOD failed");
    }
}

void EventLoop::remove(int fd) {
    // EPOLL_CTL_DEL ignores the event parameter (can be nullptr in newer kernels)
    // but older kernels requrie non-null so we pass dummy
    epoll_event ev{};
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, &ev);  // ignore errors (fd might already be closed)

    callbacks_.erase(fd);
}

int EventLoop::poll(int timeout_ms) {
    epoll_event events[kMaxEvents];

    int n = epoll_wait(epoll_fd_, events, kMaxEvents, timeout_ms);
    if (n < 0) {
        if (errno == EINTR) {
            return 0;  // interrupted by signal, not error
        }
        return -1;
    }

    for (int i = 0; i < n; ++i) {
        int fd = events[i].data.fd;
        uint32_t ev = events[i].events;

        auto it = callbacks_.find(fd);
        if (it != callbacks_.end()) {
            try {
                it->second(fd, ev);
            } catch (const std::exception& e) {
                LOG_ERROR("EventLoop callback error: " + std::string(e.what()));
            }
        }
    }

    return n;
}

void EventLoop::run() {
    running_ = true;
    while (running_) {
        poll(100);  // 100ms timeout to check running_ flag periodically
    }
}

void EventLoop::stop() {
    running_ = false;
}

bool EventLoop::running() const noexcept {
    return running_;
}

}  // namespace kvstore::net::server