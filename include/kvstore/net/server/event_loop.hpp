#ifndef KVSTORE_NET_SERVER_EVENT_LOOP_HPP
#define KVSTORE_NET_SERVER_EVENT_LOOP_HPP

#include <sys/epoll.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace kvstore::net::server {
/*
    eventloop wraps Linux epoll for event driven IO
    usage:
        EventLoop loop;
        loop.add(server_fd, EPOLLIN, [](int fd, uint32_t events)) {
            //handle new connection
        });
        loop.run(); //blocks until stop() is called

    key conecpts:
        - epoll_create1() creates an epoll instance (returns fd)
        - epoll_ctl() adds/modifies/removes fds to watch
        - epoll_wait() blocks until events are ready
        - EPOLLIN = ready to read, EPOLLOUT = ready to write
        - EPOLLET = edge-triggered (notify once per state change, not continuously)
*/

class EventLoop {
public:
    //callback signature: (fd, events) where event is EPOLLIN/EPOLLOUT/EPOLLERR/etc
    using Callback = std::function<void(int fd, uint32_t events)>;

    EventLoop();
    ~EventLoop();

    // non-copyable, non-moveable (owns epoll fd and callbacks)
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    EventLoop(EventLoop&&) = delete;
    EventLoop& operator=(EventLoop&&) = delete;

    //add fd to watch with given events 
    //callback invoked when events occur
    void add(int fd, uint32_t events, Callback callback);

    //modify events for existing fd
    void modify(int fd, uint32_t events);

    //remove fd from watch list
    void remove(int fd);

    //run 1 iteration of event loop
    //return number of events processed or -1 on error
    //timeout_ms: -1 = block forever, 0 = return immediately, >0 = wait up to N ms
    int poll(int timeout_ms = -1);

    // run event loop until stop() is called
    void run();

    //signal event loop to stop (thread-safe)
    void stop();

    //check if event loop is running
    [[nodiscard]] bool running () const noexcept;

private:
    static constexpr int kMaxEvents = 64;
    
    int epoll_fd_ {-1};
    std::atomic<bool> running_{false};
    std::unordered_map<int, Callback> callbacks_;
};

} //namespace kvstore::net::server

#endif