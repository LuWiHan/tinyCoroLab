#include "coro/engine.hpp"
#include "config.h"
#include "coro/io/io_info.hpp"
#include "coro/meta_info.hpp"
#include "coro/task.hpp"
#include <coroutine>
#include <cstdint>

namespace coro::detail
{
using std::memory_order_relaxed;

engine::engine() noexcept
{
    m_id = ginfo.engine_id.fetch_add(1, std::memory_order_relaxed);
    m_efd = eventfd(0, 0);
    if (m_efd < 0)
    {
        log::error("engine init event_fd failed");
        std::exit(1);
    }
}

engine::~engine() noexcept
{
    close(m_efd);
}

auto engine::init() noexcept -> void
{
    m_upxy.init(config::kEntryLength,m_efd);
    linfo.egn = this;
    // TODO[lab2a]: Add you codes
}

auto engine::deinit() noexcept -> void
{
    m_upxy.deinit();
    linfo.egn = nullptr;
    // TODO[lab2a]: Add you codes
}

auto engine::ready() noexcept -> bool
{
    // TODO[lab2a]: Add you codes
    return !m_task_queue.was_empty();
}

auto engine::get_free_urs() noexcept -> ursptr
{
    // TODO[lab2a]: Add you codes
    return m_upxy.get_free_sqe();
}

auto engine::num_task_schedule() noexcept -> size_t
{
    // TODO[lab2a]: Add you codes
    return m_task_queue.was_size();
}

auto engine::schedule() noexcept -> coroutine_handle<>
{
    // TODO[lab2a]: Add you codes
    coroutine_handle<> coro_handle{nullptr};
    bool               is_success = m_task_queue.try_pop(coro_handle);
    return (is_success ? coro_handle : std::noop_coroutine());
}

auto engine::submit_task(coroutine_handle<> handle) noexcept -> void
{
    // TODO[lab2a]: Add you codes
    m_task_queue.push(handle);
    notify();
}

auto engine::exec_one_task() noexcept -> void
{
    auto coro = schedule();
    coro.resume();
    if (coro.done())
    {
        clean(coro);
    }
}

auto engine::handle_cqe_entry(urcptr cqe) noexcept -> void
{
    auto data = reinterpret_cast<io::detail::io_info*>(io_uring_cqe_get_data(cqe));
    data->cb(data, cqe->res);
}

auto engine::poll_submit() noexcept -> void
{
    // TODO[lab2a]: Add you codes
    // 1.提交io任务
    m_upxy.submit();
    m_running_io += m_submit_io;
    m_submit_io = 0;

    // 2.处理已完成的io任务
    int cqe_num = m_upxy.peek_batch_cqe(m_urc.data(), m_urc.size());
    for (int i = 0; i < cqe_num; ++i)
        handle_cqe_entry(m_urc[i]);
    m_upxy.cq_advance(cqe_num);
    m_running_io -= cqe_num;
}

auto engine::add_io_submit() noexcept -> void
{
    m_submit_io++;
    // TODO[lab2a]: Add you codes
}

auto engine::empty_io() noexcept -> bool
{
    // TODO[lab2a]: Add you codes
    return (m_submit_io == 0 && m_running_io == 0);
}

auto engine::notify() noexcept -> void
{
    auto ret = eventfd_write(m_efd, 1);
    assert(ret != -1 && "eventfd write error");
}

auto engine::wait_task() noexcept -> uint64_t
{
    uint64_t res;
    auto     ret = eventfd_read(m_efd ,&res);
    assert(ret != -1 && "eventfd read error");
    return res;
}
}; // namespace coro::detail
