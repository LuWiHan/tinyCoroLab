/**
 * @file wait_group.hpp
 * @author JiahuiWang
 * @brief lab4c
 * @version 1.1
 * @date 2025-03-24
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include <atomic>
#include <coroutine>
#include <vector>

#include "coro/detail/types.hpp"
#include "coro/spinlock.hpp"
#include "coro/context.hpp"

namespace coro
{
/**
 * @brief Welcome to tinycoro lab4c, in this part you will build the basic coroutine
 * synchronization component——wait_group by modifing wait_group.hpp and wait_group.cpp.
 * Please ensure you have read the document of lab4c.
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

// TODO[lab4c]: This wait_group is an example to make complie success,
// You should delete it and add your implementation, I don't care what you do,
// but keep the member function and construct function's declaration same with example.
class wait_group
{
    struct waitting_element
    {
        context* ctx;
        std::coroutine_handle<> handle;
    };

    struct awaiter
    {
        awaiter(wait_group* g) : m_group(g){}
        auto await_ready() -> bool { return m_group->m_flag.load(std::memory_order_relaxed); }
        auto await_suspend(std::coroutine_handle<> h) -> bool
        {
            std::lock_guard<detail::spinlock> lock(m_group->m_lock);
            if(m_group->m_flag.load(std::memory_order_acquire))
                return false;
            waitting_element element;
            element.ctx = detail::linfo.ctx;
            element.handle = h;
            m_group->m_wait_queue.push_back(element);
            m_register_cnt = 1;
            element.ctx->register_wait(m_register_cnt);
            return true;
        }
        auto await_resume() -> void { detail::linfo.ctx->unregister_wait(m_register_cnt); }

        wait_group*  m_group;
        int     m_register_cnt{0};
    };

public:
    explicit wait_group(int count = 0) noexcept :m_cnt(count) {}

    auto add(int count) noexcept -> void 
    {
        uint64_t cnt = m_cnt.fetch_add(count,std::memory_order_relaxed);
        if(cnt+count == 0)
        {
            // 1.????
            m_flag.store(true,std::memory_order_release);
            // 2.????
            std::lock_guard<detail::spinlock> lock(m_lock);
            for(auto& e:m_wait_queue)
                e.ctx->submit_task(e.handle);
        }
    };

    auto done() noexcept -> void 
    {
        add(-1);
    };

    auto wait() noexcept -> awaiter { return {this}; };
private:
    std::atomic_uint64_t            m_cnt;
    std::atomic_bool                m_flag{false};
    detail::spinlock                m_lock;
    std::vector<waitting_element>   m_wait_queue;
};

}; // namespace coro
