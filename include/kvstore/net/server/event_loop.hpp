#ifndef KVSTORE_NET_SERVER_EVENT_LOOP_HPP
#define KVSTORE_NET_SERVER_EVENT_LOOP_HPP

#ifdef __linux__
#include <sys/epoll.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#include <sys/time.h>
#endif

#include <atomic>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "kvstore/util/fd_guard.hpp"

// Portable event flags (map to epoll/kqueue equivalents)
namespace kvstore::net::server {
#ifdef __linux__
constexpr uint32_t kEventRead = EPOLLIN;
constexpr uint32_t kEventWrite = EPOLLOUT;
constexpr uint32_t kEventError = EPOLLERR;
constexpr uint32_t kEventHangup = EPOLLHUP;
#elif defined(__APPLE__) || defined(__FreeBSD__)
// kqueue uses separate filters, so we define our own flags
constexpr uint32_t kEventRead = 1 << 0;
constexpr uint32_t kEventWrite = 1 << 1;
constexpr uint32_t kEventError = 1 << 2;
constexpr uint32_t kEventHangup = 1 << 3;
#endif
}  // namespace kvstore::net::server

namespace kvstore::net::server {
/*
    EventLoop wraps platform-specific event notification (epoll on Linux, kqueue on macOS/BSD)
    usage:
        EventLoop loop;
        loop.add(server_fd, kEventRead, [](int fd, uint32_t events)) {
            //handle new connection
        });
        loop.run(); //blocks until stop() is called

    portable event flags:
        - kEventRead = ready to read
        - kEventWrite = ready to write
        - kEventError = error occurred
        - kEventHangup = peer closed connection
*/

class EventLoop {
   public:
    // callback signature: (fd, events) where events is a bitmask of kEventRead/kEventWrite/etc
    using Callback = std::function<void(int fd, uint32_t events)>;

    EventLoop();
    ~EventLoop();

    // non-copyable, non-moveable (owns event fd and callbacks)
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    EventLoop(EventLoop&&) = delete;
    EventLoop& operator=(EventLoop&&) = delete;

    // add fd to watch with given events
    // callback invoked when events occur
    void add(int fd, uint32_t events, Callback callback);

    // modify events for existing fd
    void modify(int fd, uint32_t events);

    // remove fd from watch list
    void remove(int fd);

    // run 1 iteration of event loop
    // return number of events processed or -1 on error
    // timeout_ms: -1 = block forever, 0 = return immediately, >0 = wait up to N ms
    int poll(int timeout_ms = -1);

    // run event loop until stop() is called
    void run();

    // signal event loop to stop (thread-safe)
    void stop();

    // check if event loop is running
    [[nodiscard]] bool running() const noexcept;

   private:
    void process_pending_removals();

    static constexpr int kMaxEvents = 64;

    util::FdGuard event_fd_;  // epoll fd on Linux, kqueue fd on macOS/BSD
    std::atomic<bool> running_{false};
    std::unordered_map<int, Callback> callbacks_;
#if defined(__APPLE__) || defined(__FreeBSD__)
    std::unordered_map<int, uint32_t> fd_events_;  // track events per fd for kqueue
#endif
    std::vector<int>
        pending_removals_;     // deferred removals to avoid destroying callback while executing
    bool in_callback_{false};  // true while executing a callback
};

}  // namespace kvstore::net::server

#endif