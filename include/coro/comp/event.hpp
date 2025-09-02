/**
 * @file event.hpp
 * @author JiahuiWang
 * @brief lab4a
 * @version 1.1
 * @date 2025-03-24
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once
#include <atomic>
#include <coroutine>
#include <mutex>
#include <vector>

#include "coro/attribute.hpp"
#include "coro/concepts/awaitable.hpp"
#include "coro/context.hpp"
#include "coro/detail/container.hpp"
#include "coro/detail/types.hpp"
#include "coro/meta_info.hpp"
#include "coro/spinlock.hpp"

namespace coro
{
/**
 * @brief Welcome to tinycoro lab4a, in this part you will build the basic coroutine
 * synchronization component - event by modifing event.hpp and event.cpp. Please ensure
 * you have read the document of lab4a.
 *
 * @warning You should carefully consider whether each implementation should be thread-safe.
 *
 * You should follow the rules below in this part:
 *
 * @note The location marked by todo is where you must add code, but you can also add code anywhere
 * you want, such as function and class definitions, even member variables.
 *
 * @note lab4 and lab5 are free designed lab, leave the interfaces that the test case will use,
 * and then, enjoy yourself!
 */
class context;

namespace detail
{
// TODO[lab4a]: Add code that you don't want to use externally in namespace detail
struct waitting_element
{
    context* ctx;
    std::coroutine_handle<> handle;
};
class event_base
{
protected:
    struct awaiter_base
    {
        awaiter_base(event_base* e)
            : m_event(e){}
        auto await_ready() -> bool { return m_event->m_flag.load(std::memory_order_relaxed); }
        auto await_suspend(std::coroutine_handle<> h) -> bool
        {
            // 加入等待队列
            std::lock_guard<detail::spinlock> lock(m_event->m_lock);
            bool flag = m_event->m_flag.load(std::memory_order_acquire);
            if(flag)
                return false;
            detail::waitting_element element;
            element.ctx = detail::linfo.ctx;
            element.handle = h;
            m_event->m_wait_queue.push_back(element);
            m_register_cnt = 1;
            element.ctx->register_wait(m_register_cnt);

            return true;

        }
        auto await_resume() -> void 
        { 
            detail::linfo.ctx->unregister_wait(m_register_cnt);
        }
        event_base* m_event;
        int m_register_cnt{0};
    };

    inline auto notify_all() -> void
    {
        //1.设置标志
        m_flag.store(true,std::memory_order_release);

        // 2.唤醒suspend协程
        std::lock_guard<detail::spinlock> lock(m_lock);
        for(int i=0;i<m_wait_queue.size();++i)
        {
            m_wait_queue[i].ctx->submit_task(m_wait_queue[i].handle);
        }
    }

protected:
    std::vector<detail::waitting_element>    m_wait_queue;
    detail::spinlock        m_lock;
    std::atomic_bool        m_flag{false};
};

}; // namespace detail

// TODO[lab4a]: This event is an example to make complie success,
// You should delete it and add your implementation, I don't care what you do,
// but keep the function set() and wait()'s declaration same with example.
template<typename return_type = void>
class event : public detail::event_base
{
    
    // Just make compile success
    struct awaiter : public awaiter_base
    {
        awaiter(event* e)
            : awaiter_base(e){}
        auto await_resume() -> return_type 
        { 
            awaiter_base::await_resume();
            return static_cast<event*>(m_event)->m_value; 
        }
    };

public:
    auto wait() noexcept -> awaiter { return {this}; } // return awaitable

    template<typename value_type>
    auto set(value_type&& value) noexcept -> void
    {
        m_value = value;
        notify_all();
    }
private:
    return_type             m_value;
};

template<>
class event<> : public detail::event_base
{
    using awaiter = awaiter_base;

public:
    auto wait() noexcept -> awaiter { return {this}; } // return awaitable
    auto set() noexcept -> void { notify_all(); }
};

/**
 * @brief RAII for event
 *
 */
class event_guard
{
    using guard_type = event<>;

public:
    event_guard(guard_type& ev) noexcept : m_ev(ev) {}
    ~event_guard() noexcept { m_ev.set(); }

private:
    guard_type& m_ev;
};

}; // namespace coro
