#include "kvstore/net/server/event_loop.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <stdexcept>

#include "kvstore/util/logger.hpp"

namespace kvstore::net::server {

EventLoop::EventLoop() {
#ifdef __linux__
    // EPOLL_CLOEXEC: close epoll fd on exec() - prevents leaking fd to child processes
    event_fd_.reset(epoll_create1(EPOLL_CLOEXEC));
    if (event_fd_.get() < 0) {
        throw std::runtime_error("failed to create epoll instance");
    }
#elif defined(__APPLE__) || defined(__FreeBSD__)
    event_fd_.reset(kqueue());
    if (event_fd_.get() < 0) {
        throw std::runtime_error("failed to create kqueue instance");
    }
    // Set close-on-exec flag manually for kqueue
    fcntl(event_fd_.get(), F_SETFD, FD_CLOEXEC);
#endif
}

EventLoop::~EventLoop() = default;  // FdGuard handles fd cleanup

void EventLoop::add(int fd, uint32_t events, Callback callback) {
#ifdef __linux__
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(event_fd_.get(), EPOLL_CTL_ADD, fd, &ev) < 0) {
        throw std::runtime_error("epoll_ctl ADD failed");
    }
#elif defined(__APPLE__) || defined(__FreeBSD__)
    struct kevent changes[2];
    int nchanges = 0;

    if (events & kEventRead) {
        EV_SET(&changes[nchanges++], fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }
    if (events & kEventWrite) {
        EV_SET(&changes[nchanges++], fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }

    if (nchanges > 0) {
        if (kevent(event_fd_.get(), changes, nchanges, nullptr, 0, nullptr) < 0) {
            throw std::runtime_error("kevent ADD failed");
        }
    }
    fd_events_[fd] = events;
#endif

    callbacks_[fd] = std::move(callback);
}

void EventLoop::modify(int fd, uint32_t events) {
#ifdef __linux__
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(event_fd_.get(), EPOLL_CTL_MOD, fd, &ev) < 0) {
        throw std::runtime_error("epoll_ctl MOD failed");
    }
#elif defined(__APPLE__) || defined(__FreeBSD__)
    uint32_t old_events = fd_events_[fd];
    struct kevent changes[4];
    int nchanges = 0;

    // Disable filters that are no longer wanted
    if ((old_events & kEventRead) && !(events & kEventRead)) {
        EV_SET(&changes[nchanges++], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    }
    if ((old_events & kEventWrite) && !(events & kEventWrite)) {
        EV_SET(&changes[nchanges++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    }

    // Enable filters that are newly wanted
    if (!(old_events & kEventRead) && (events & kEventRead)) {
        EV_SET(&changes[nchanges++], fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }
    if (!(old_events & kEventWrite) && (events & kEventWrite)) {
        EV_SET(&changes[nchanges++], fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }

    if (nchanges > 0) {
        if (kevent(event_fd_.get(), changes, nchanges, nullptr, 0, nullptr) < 0) {
            throw std::runtime_error("kevent MOD failed");
        }
    }
    fd_events_[fd] = events;
#endif
}

void EventLoop::remove(int fd) {
#ifdef __linux__
    // EPOLL_CTL_DEL ignores the event parameter (can be nullptr in newer kernels)
    // but older kernels require non-null so we pass dummy
    epoll_event ev{};
    epoll_ctl(event_fd_.get(), EPOLL_CTL_DEL, fd, &ev);  // ignore errors (fd might already be closed)
#elif defined(__APPLE__) || defined(__FreeBSD__)
    // Remove both read and write filters (ignore errors - fd might already be closed)
    struct kevent changes[2];
    EV_SET(&changes[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&changes[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    kevent(event_fd_.get(), changes, 2, nullptr, 0, nullptr);
    fd_events_.erase(fd);
#endif

    // !!!!!IMPORTANT!!!!: If we're inside a callback, defer the removal to avoid destroying the
    // callback while executing
    if (in_callback_) {
        pending_removals_.push_back(fd);
    } else {
        callbacks_.erase(fd);
    }
}

void EventLoop::process_pending_removals() {
    for (int fd : pending_removals_) {
        callbacks_.erase(fd);
    }
    pending_removals_.clear();
}

int EventLoop::poll(int timeout_ms) {
#ifdef __linux__
    epoll_event events[kMaxEvents];

    int n = epoll_wait(event_fd_.get(), events, kMaxEvents, timeout_ms);
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
                in_callback_ = true;
                it->second(fd, ev);
                in_callback_ = false;
                process_pending_removals();
            } catch (const std::exception& e) {
                in_callback_ = false;
                process_pending_removals();
                LOG_ERROR("EventLoop callback error: " + std::string(e.what()));
            }
        }
    }

    return n;
#elif defined(__APPLE__) || defined(__FreeBSD__)
    struct kevent events[kMaxEvents];
    struct timespec ts;
    struct timespec* ts_ptr = nullptr;

    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000;
        ts_ptr = &ts;
    }

    int n = kevent(event_fd_.get(), nullptr, 0, events, kMaxEvents, ts_ptr);
    if (n < 0) {
        if (errno == EINTR) {
            return 0;  // interrupted by signal, not error
        }
        return -1;
    }

    // Aggregate events per fd since kqueue reports read/write separately
    std::unordered_map<int, uint32_t> aggregated;
    for (int i = 0; i < n; ++i) {
        int fd = static_cast<int>(events[i].ident);
        uint32_t ev = 0;

        if (events[i].filter == EVFILT_READ) {
            ev |= kEventRead;
        } else if (events[i].filter == EVFILT_WRITE) {
            ev |= kEventWrite;
        }

        if (events[i].flags & EV_EOF) {
            ev |= kEventHangup;
        }
        if (events[i].flags & EV_ERROR) {
            ev |= kEventError;
        }

        aggregated[fd] |= ev;
    }

    for (const auto& [fd, ev] : aggregated) {
        auto it = callbacks_.find(fd);
        if (it != callbacks_.end()) {
            try {
                in_callback_ = true;
                it->second(fd, ev);
                in_callback_ = false;
                process_pending_removals();
            } catch (const std::exception& e) {
                in_callback_ = false;
                process_pending_removals();
                LOG_ERROR("EventLoop callback error: " + std::string(e.what()));
            }
        }
    }

    return n;
#endif
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