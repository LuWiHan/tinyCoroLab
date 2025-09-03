#include "coro/comp/mutex.hpp"
#include "coro/meta_info.hpp"
#include "coro/scheduler.hpp"
#include "coro/spinlock.hpp"
#include <atomic>
#include <mutex>

namespace coro
{

auto mutex::awaiter::await_ready() -> bool 
{
    return m_mtx->try_lock();
}
auto mutex::awaiter::await_suspend(std::coroutine_handle<> h) -> bool
{
    std::lock_guard<detail::spinlock> lock(m_mtx->m_spinlock);
    // 1.再次尝试获取锁
    bool res = m_mtx->try_lock();
    if(res)
        return false;
    
    // 2.未获取到锁加入到wait队列
    waitting_element element;
    element.ctx = detail::linfo.ctx;
    element.handle = h;
    m_mtx->m_wait_queue.push(element);
    m_register_cnt = 1;
    element.ctx->register_wait(m_register_cnt);
    return true;
}

// TODO[lab4d] : Add codes if you need
auto mutex::try_lock() noexcept -> bool 
{
    bool expected = false; 
    bool res = m_lock_state.compare_exchange_weak(expected, true,std::memory_order_relaxed);
    return res; 
}

auto mutex::unlock() noexcept -> void 
{
    if(is_lock())
    {
        std::lock_guard<detail::spinlock> lock(m_spinlock);
        if(m_wait_queue.empty())
        {
            m_lock_state.store(false,std::memory_order_relaxed);
            return;
        }

        waitting_element& element = m_wait_queue.front();
        element.ctx->submit_task(element.handle);
        m_wait_queue.pop();
    }
}

auto mutex::is_lock() noexcept -> bool
{
    return m_lock_state.load(std::memory_order_relaxed);
}

}; // namespace coro