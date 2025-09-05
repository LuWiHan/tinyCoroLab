#include "coro/comp/condition_variable.hpp"
#include "coro/meta_info.hpp"
#include "coro/scheduler.hpp"
#include "coro/spinlock.hpp"
#include <mutex>

namespace coro
{
// TODO[lab5b] : Add codes if you need
auto condition_variable::awaiter::await_ready() -> bool 
{
    return false;
}

auto condition_variable::awaiter::await_suspend(std::coroutine_handle<> h) -> void
{
    waitting_element e;
    e.ctx = detail::linfo.ctx;
    e.handle = h;
    m_register_cnt = 1;
    m_cv->push_queue(e);
    e.ctx->register_wait(m_register_cnt);
    m_mtx.unlock();
}

auto condition_variable::wait(mutex& mtx) noexcept -> task<>
{
    co_await awaiter{this,mtx};
    co_await mtx.lock();
    co_return;
}

auto condition_variable::wait(mutex& mtx, cond_type&& cond) noexcept -> task<>
{
    while(!cond())
    {
        co_await awaiter{this,mtx};
        co_await mtx.lock();
    }
    co_return;
}

auto condition_variable::wait(mutex& mtx, cond_type& cond) noexcept -> task<>
{
    while(!cond())
    {
        co_await awaiter{this,mtx};
        co_await mtx.lock();
    }
    co_return;
}

auto condition_variable::notify_one() noexcept -> void
{
    waitting_element e;
    bool ret = pop_queue(e);
    if(ret)
        e.ctx->submit_task(e.handle);
}

auto condition_variable::notify_all() noexcept -> void
{
    waitting_element e;
    while(pop_queue(e))
        e.ctx->submit_task(e.handle);
}

auto condition_variable::push_queue(waitting_element& e) noexcept -> void
{
    std::lock_guard<detail::spinlock> lock(m_spinlock);
    m_wait_queue.push(e);
}

auto condition_variable::pop_queue(waitting_element& e) noexcept -> bool
{
    std::lock_guard<detail::spinlock> lock(m_spinlock);
    if(m_wait_queue.empty())
        return false;
    e = m_wait_queue.front();
    m_wait_queue.pop();
    return true;
}
} // namespace coro
