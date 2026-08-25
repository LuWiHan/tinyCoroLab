#include <liburing.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <vector>
#include <thread>
#include <memory>
#include <iostream>
#include <cstring>
#include <cassert>
#include <arpa/inet.h>
#include <algorithm>
#include <cstdlib>
#include <queue>
#include <mutex>

// Define buffer size to match tinycoro benchmark
#define BUFFLEN (1024 + 16)
#define URING_QUEUE_DEPTH 4096

// Helper to set non-blocking
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

enum TokenType {
    TOKEN_READ,
    TOKEN_WRITE,
    TOKEN_EVENTFD
};

struct ConnData {
    int fd;
    TokenType type;
    char buf[BUFFLEN];
};

class Worker {
public:
    Worker() {
        if (io_uring_queue_init(uring_queue_depth_, &ring_, 0) < 0) {
            perror("io_uring_queue_init");
            exit(1);
        }

        event_fd_ = eventfd(0, EFD_NONBLOCK);
        if (event_fd_ < 0) {
            perror("eventfd");
            exit(1);
        }

        // Prepare eventfd read
        submit_event_fd();
    }

    ~Worker() {
        running_ = false;
        // In a real app we would wake up worker to exit, but this is a bench
        if (thread_.joinable()) thread_.join();
        io_uring_queue_exit(&ring_);
        close(event_fd_);
    }

    void start() {
        running_ = true;
        thread_ = std::thread(&Worker::loop, this);
    }

    void add_connection(int fd) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            new_connections_.push(fd);
        }
        uint64_t u = 1;
        if (write(event_fd_, &u, sizeof(u)) != sizeof(u)) {
            perror("write eventfd");
        }
    }

private:
    struct io_uring ring_;
    int event_fd_;
    uint64_t event_buf_;
    std::thread thread_;
    bool running_ = false;
    const unsigned uring_queue_depth_ = URING_QUEUE_DEPTH;

    std::mutex queue_mutex_;
    std::queue<int> new_connections_;
    
    // For eventfd token
    ConnData event_token_ = {0, TOKEN_EVENTFD, {0}};

    void submit_event_fd() {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
        if (!sqe) {
            fprintf(stderr, "EventFD SQE full\n");
            return;
        }
        io_uring_prep_read(sqe, event_fd_, &event_buf_, sizeof(event_buf_), 0);
        io_uring_sqe_set_data(sqe, &event_token_);
        io_uring_submit(&ring_); // Ensure wakeup mechanism is armed
    }

    void loop() {
        while (running_) {
            int ret = io_uring_submit_and_wait(&ring_, 1);
            if (ret < 0) {
                if (ret == -EINTR) continue;
                fprintf(stderr, "io_uring_submit_and_wait: %s\n", strerror(-ret));
                break;
            }

            struct io_uring_cqe *cqe;
            unsigned head;
            unsigned count = 0;

            io_uring_for_each_cqe(&ring_, head, cqe) {
                count++;
                ConnData *data = (ConnData*)io_uring_cqe_get_data(cqe);
                int res = cqe->res;

                if (data->type == TOKEN_EVENTFD) {
                    if (res < 0) {
                        fprintf(stderr, "eventfd read error: %d\n", res);
                    }
                    handle_new_connections();
                    
                    // Rearm eventfd
                    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
                    if (sqe) {
                        io_uring_prep_read(sqe, event_fd_, &event_buf_, sizeof(event_buf_), 0);
                        io_uring_sqe_set_data(sqe, &event_token_);
                    }
                } else if (data->type == TOKEN_READ) {
                    if (res <= 0) {
                        // EOF or Error
                        close(data->fd);
                        delete data;
                    } else {
                        // Echo back
                        data->type = TOKEN_WRITE;
                        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
                        if (sqe) {
                            io_uring_prep_send(sqe, data->fd, data->buf, res, 0);
                            io_uring_sqe_set_data(sqe, data);
                        } else {
                            // Ring full, should handle, but simplified here
                            close(data->fd);
                            delete data;
                        }
                    }
                } else if (data->type == TOKEN_WRITE) {
                    if (res < 0) {
                        close(data->fd);
                        delete data;
                    } else {
                        // Prepare next read
                        data->type = TOKEN_READ;
                        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
                        if (sqe) {
                            io_uring_prep_recv(sqe, data->fd, data->buf, BUFFLEN, 0);
                            io_uring_sqe_set_data(sqe, data);
                        } else {
                           close(data->fd);
                           delete data;
                        }
                    }
                }
            }
            io_uring_cq_advance(&ring_, count);
        }
    }

    void handle_new_connections() {
        std::queue<int> conns;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            conns.swap(new_connections_);
        }

        while (!conns.empty()) {
            int fd = conns.front();
            conns.pop();
            set_nonblocking(fd);

            ConnData *data = new ConnData();
            data->fd = fd;
            data->type = TOKEN_READ;
            
            struct io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
            if (sqe) {
                io_uring_prep_recv(sqe, fd, data->buf, BUFFLEN, 0);
                io_uring_sqe_set_data(sqe, data);
            } else {
                close(fd);
                delete data;
            }
        }
    }
};

int main(int argc, char* argv[]) {
    int port = 7000;
    int thread_num = std::thread::hardware_concurrency();

    if (argc > 1) port = std::atoi(argv[1]);
    if (argc > 2) thread_num = std::atoi(argv[2]);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        perror("bind");
        return 1;
    }

    if (listen(listen_fd, 4096) != 0) {
        perror("listen");
        return 1;
    }

    std::vector<std::unique_ptr<Worker>> workers;
    for (int i = 0; i < thread_num; ++i) {
        auto worker = std::make_unique<Worker>();
        worker->start();
        workers.push_back(std::move(worker));
    }

    std::cout << "IO_URING Echo Server listening on port " << port 
              << " with " << thread_num << " threads." << std::endl;

    size_t round_robin = 0;
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &len);
        
        if (client_fd < 0) {
             perror("accept");
             continue;
        }

        workers[round_robin % workers.size()]->add_connection(client_fd);
        round_robin++;
    }

    return 0;
}
