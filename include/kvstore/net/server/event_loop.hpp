class EventLoop {
public:
    using Callback = std::function<void(int fd, uint32_t events)>;

    EventLoop();
    ~EventLoop();

    //Register fd with callback (EPOLLIN | EPOLLOUT | EPOLLET)
    void add(int fd, uin32_t events, Callback cb);
    void modify(int fd, uint32_t events);
    void remove(int fd)l

    // run one iteration, returns number of events processed
    int poll(int timeout_ms = -1);

    //run until stop() is called
    void run();
    void stop();

private:
    int epoll_fd_;
    std::atomic<bool> running {false};
    std::unordered_map<int, Callback> callbacks_;
};