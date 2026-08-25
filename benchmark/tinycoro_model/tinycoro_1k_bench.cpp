#include "coro/coro.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace coro;

// Uncomment the following line to use the coroutine-based listener (Original Mode)
#define USE_CORO_LISTENER

#define BUFFLEN 1024
#define MAX_THREADS_NUM 200

#include <atomic>
#include <liburing.h>

class ChainEchoAwaiter
{
public:
    struct ChainInfo : public coro::io::detail::io_info
    {
        ChainEchoAwaiter* self;
    };

    ChainEchoAwaiter(int fd, char* buf, size_t len)
        : m_fd(fd), m_buf(buf), m_len(len)
    {
    }

    constexpr bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        m_handle = handle;

        m_read_info.type   = coro::io::detail::io_type::tcp_read;
        m_read_info.cb     = &ChainEchoAwaiter::callback_read;
        m_read_info.self   = this;
        m_read_info.handle = handle;

        m_write_info.type   = coro::io::detail::io_type::tcp_write;
        m_write_info.cb     = &ChainEchoAwaiter::callback_write;
        m_write_info.self   = this;
        m_write_info.handle = handle;

        auto& engine = coro::detail::local_engine();

        auto sqe1 = engine.get_free_urs();
        io_uring_prep_recv(sqe1, m_fd, m_buf, m_len, 0);
        io_uring_sqe_set_flags(sqe1, IOSQE_IO_LINK);
        io_uring_sqe_set_data(sqe1, &m_read_info);
        engine.add_io_submit();

        auto sqe2 = engine.get_free_urs();
        io_uring_prep_send(sqe2, m_fd, m_buf, m_len, MSG_NOSIGNAL);
        io_uring_sqe_set_flags(sqe2, 0);
        io_uring_sqe_set_data(sqe2, &m_write_info);
        engine.add_io_submit();
    }

    int await_resume() noexcept
    {
        if (m_read_res <= 0) return m_read_res;
        return m_write_res;
    }

private:
    static void callback_read(coro::io::detail::io_info* info, int res)
    {
        auto* self       = static_cast<ChainInfo*>(info)->self;
        self->m_read_res = res;
        self->check_resume();
    }

    static void callback_write(coro::io::detail::io_info* info, int res)
    {
        auto* self        = static_cast<ChainInfo*>(info)->self;
        self->m_write_res = res;
        self->check_resume();
    }

    void check_resume()
    {
        if (m_counter.fetch_add(1, std::memory_order_acq_rel) == 1)
        {
            submit_to_context(m_handle);
        }
    }

    int                     m_fd;
    char*                   m_buf;
    size_t                  m_len;
    std::coroutine_handle<> m_handle;

    ChainInfo m_read_info;
    ChainInfo m_write_info;

    int m_read_res = 0;
    int m_write_res = 0;

    std::atomic<int> m_counter{0};
};

task<> session(int fd)
{
    char buf[BUFFLEN] = {0};
    int  ret          = 0;

    while (true)
    {
        ret = co_await ChainEchoAwaiter(fd, buf, BUFFLEN);
        if (ret <= 0)
        {
            break;
        }
    }

    co_await io::net::tcp::tcp_close_awaiter(fd);
}

task<> server(int port)
{
    auto server = io::net::tcp::tcp_server(port);
    log::info("server start in {}", port);
    int client_fd;
    while ((client_fd = co_await server.accept()) > 0)
    {
        submit_to_scheduler(session(client_fd));
    }
}

int main(int argc, char const* argv[])
{
    scheduler::init();

#ifdef USE_CORO_LISTENER
    /* code */
    submit_to_scheduler(server(8000));
    scheduler::loop();
#else
    // Main thread listener mode
    int port = 8000;
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        return 1;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    // Larger backlog for benchmark
    if (listen(listen_fd, 4096) < 0) {
        perror("listen");
        return 1;
    }

    log::info("server start in {} (Main Thread Listener)", port);
    
    while(true) {
        struct sockaddr_in cli_addr{};
        socklen_t len = sizeof(cli_addr);
        // Blocking accept in main thread
        int client_fd = accept(listen_fd, (struct sockaddr*)&cli_addr, &len);
        if (client_fd > 0) {
            // Hand over to scheduler
            submit_to_scheduler(session(client_fd));
        } else {
            if (errno == EINTR) continue;
            perror("accept");
        }
    }
    
    // Unreachable in this loop, but required if we ever break
    scheduler::loop();
#endif
    return 0;
}
