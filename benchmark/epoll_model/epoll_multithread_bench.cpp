#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <thread>
#include <memory>
#include <iostream>
#include <cstring>
#include <cassert>
#include <arpa/inet.h>
#include <algorithm>
#include <cstdlib>

// Define buffer size to match tinycoro benchmark
#define BUFFLEN (1024 + 16)
#define MAX_EVENTS 1024

// Helper to set non-blocking
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

class Worker {
public:
    Worker() {
        epoll_fd_ = epoll_create1(0);
        if (epoll_fd_ == -1) {
            perror("epoll_create1");
            exit(1);
        }
    }

    ~Worker() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        close(epoll_fd_);
    }

    void start() {
        running_ = true;
        thread_ = std::thread(&Worker::loop, this);
    }

    // Called by main thread to assign a new connection to this worker
    void add_connection(int fd) {
        set_nonblocking(fd);
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET; // Edge Triggered
        ev.data.fd = fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
            perror("epoll_ctl: add_connection");
            close(fd);
        }
    }

private:
    void loop() {
        struct epoll_event events[MAX_EVENTS];
        // Per-thread buffer for reading/writing. 
        // Note: In a real server handling partial packets, we'd need per-connection buffers.
        // For this echo benchmark where we expect full request/response, this is often sufficient/comparable 
        // to simple benchmarks, though technically not fully robust against fragmentation.
        char buf[BUFFLEN];

        while (running_) {
            int n = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);
            if (n == -1) {
                if (errno == EINTR) continue;
                perror("epoll_wait");
                break;
            }

            for (int i = 0; i < n; ++i) {
                int fd = events[i].data.fd;
                uint32_t evt = events[i].events;

                if (evt & (EPOLLERR | EPOLLHUP)) {
                   close_conn(fd);
                   continue;
                }

                if (evt & EPOLLIN) {
                    handle_read(fd, buf);
                }
            }
        }
    }

    void handle_read(int fd, char* buf) {
        while (true) {
            ssize_t n = read(fd, buf, BUFFLEN);
            if (n == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break; // No more data right now
                }
                close_conn(fd);
                break;
            } else if (n == 0) {
                close_conn(fd); // EOF
                break;
            }

            for(int i = 0; i < n; ++i) {
                buf[i] = std::toupper(buf[i]);
            }

            // Echo back immediately
            ssize_t written = 0;
            while (written < n) {
                ssize_t w = write(fd, buf + written, n - written);
                if (w == -1) {
                     if (errno == EAGAIN || errno == EWOULDBLOCK) {
                         // In a robust server, we would register EPOLLOUT and return, saving state.
                         // For this benchmark, we'll just busy-wait briefly or treat as error since 
                         // buffer should be large enough for echo.
                         continue; 
                     }
                     close_conn(fd);
                     return;
                }
                written += w;
            }
        }
    }

    void close_conn(int fd) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
    }

    int epoll_fd_;
    std::thread thread_;
    volatile bool running_ = false;
};

int main(int argc, char* argv[]) {
    int port = 9000;
    // Default to hardware concurrency or a fixed number if preferred for benchmark consistency
    int num_threads = std::thread::hardware_concurrency();
    
    // Simple args parsing: <prog> [port] [threads]
    if (argc > 1) port = std::atoi(argv[1]);
    if (argc > 2) num_threads = std::atoi(argv[2]);

    std::cout << "Starting epoll multi-reactor echo server on port " << port 
              << " with " << num_threads << " worker threads." << std::endl;

    std::vector<std::unique_ptr<Worker>> workers;
    for (int i = 0; i < num_threads; ++i) {
        workers.push_back(std::make_unique<Worker>());
        workers[i]->start();
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        return 1;
    }

    if (listen(listen_fd, SOMAXCONN) == -1) {
        perror("listen");
        return 1;
    }
    
    size_t next_worker = 0;
    while (true) {
        struct sockaddr_in cli_addr;
        socklen_t len = sizeof(cli_addr);
        int conn_fd = accept(listen_fd, (struct sockaddr*)&cli_addr, &len);
        if (conn_fd == -1) {
            perror("accept");
            continue;
        }

        // Distribute connection to worker
        workers[next_worker]->add_connection(conn_fd);
        next_worker = (next_worker + 1) % workers.size();
    }

    return 0;
}
